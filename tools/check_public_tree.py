"""Check the staged publication snapshot before publishing. Prints no file contents."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import struct
import sys
import zlib

root = Path(__file__).resolve().parents[1]

def git(*args):
    return subprocess.check_output(['git', '-C', str(root), *args])

root_files = {
    '.gitignore', '.gitattributes', 'README.md', 'LICENSE', 'LICENSE.ps3recomp', 'CMakeLists.txt', 'app.rc',
    'project.json', 'decompression-verification.json', 'main.cpp', 'stubs.cpp',
    'osk.cpp', 'osk.hpp', 'osk_win32.cpp', 'l10n.cpp',
    'graphics_pacing.h', 'graphics_menu.cpp', 'graphics_timing.cpp', 'graphics_settings.cpp', 'graphics_settings.h', 'thread_diagnostics.cpp', 'graphics_diagnostics.cpp', 'spu_job_diagnostics.cpp', 'windows_guids.c', 'lwcond.cpp', 'lfqueue.cpp', 'pamf.cpp', 'mpegps.hpp', 'dmux.cpp', 'vpost.cpp', 'adec.cpp', 'vdec.cpp',
}
extensions = {'tools': {'.py', '.ps1', '.java'}, 'tests': {'.cpp', '.c'},
              'patches': {'.patch', '.json'}, 'docs': {'.md'}}
patterns = {
    'personal filesystem path': r'(?:[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/][^\s]+|/(?:Users|home)/[^\s]+)',
    'credential': r'(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|(?<![A-Za-z0-9])sk-[A-Za-z0-9_-]{20,}|AKIA[0-9A-Z]{16}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----)',
    'conversation artifact': r'(?:<(?:codex_internal_context|environment_context|in-app-browser-context|external_codex_apps_open_page)(?:\s[^>]*|)>|"role"\s*:\s*"(?:user|assistant|system)")',
}
findings = []
total = 0
png_chunks = {b'IHDR', b'IDAT', b'IEND'}

def check_png(data):
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        return 'invalid PNG signature'
    offset = 8
    kinds = []
    while offset + 12 <= len(data):
        size = struct.unpack_from('>I', data, offset)[0]
        end = offset + 12 + size
        kind = data[offset + 4:offset + 8]
        if kind not in png_chunks or end > len(data):
            return 'optional metadata, unexpected chunk or truncated PNG'
        crc = struct.unpack_from('>I', data, end - 4)[0]
        if zlib.crc32(data[offset + 4:end - 4]) & 0xffffffff != crc:
            return 'PNG checksum mismatch'
        kinds.append(kind)
        if kind == b'IHDR':
            if size != 13:
                return 'invalid PNG header'
            width, height = struct.unpack_from('>II', data, offset + 8)
            if not 0 < width <= 8192 or not 0 < height <= 8192:
                return 'unexpected screenshot dimensions'
        offset = end
        if kind == b'IEND':
            if size or offset != len(data):
                return 'trailing PNG data'
            break
    if not kinds or kinds[0] != b'IHDR' or kinds[-1] != b'IEND' or b'IDAT' not in kinds:
        return 'incomplete PNG'
    return None

def check_ico(data):
    sizes = (16, 24, 32, 48, 64, 128, 256)
    if len(data) < 6 or struct.unpack_from('<HHH', data) != (0, 1, len(sizes)):
        return 'unexpected ICO header'
    offset = 6 + 16 * len(sizes)
    if len(data) < offset:
        return 'truncated ICO directory'
    for index, size in enumerate(sizes):
        w, h, colors, reserved, planes, bits, length, start = struct.unpack_from(
            '<BBBBHHII', data, 6 + 16 * index)
        if ((w or 256), (h or 256), colors, reserved, planes, bits) != (size, size, 0, 0, 1, 32):
            return 'unexpected ICO entry'
        if start != offset or length < 24 or start + length > len(data):
            return 'invalid ICO image bounds'
        png = data[start:start + length]
        reason = check_png(png)
        if reason:
            return 'ICO image: ' + reason
        if struct.unpack_from('>II', png, 16) != (size, size):
            return 'ICO dimensions mismatch'
        offset += length
    return None if offset == len(data) else 'trailing ICO data'


def invalid_sha256_fields(value, prefix=''):
    """Unknown digests may be null; recorded digests must contain all 32 bytes."""
    invalid = []
    def check_digest(item, location):
        if isinstance(item, dict):
            for key, child in item.items():
                check_digest(child, location + '.' + key)
        elif isinstance(item, list):
            for index, child in enumerate(item):
                check_digest(child, f'{location}[{index}]')
        elif item is not None and (not isinstance(item, str) or
                                   not re.fullmatch(r'[0-9a-fA-F]{64}', item)):
            invalid.append(location)
    if isinstance(value, dict):
        for key, item in value.items():
            location = prefix + ('.' if prefix else '') + key
            if key == 'sha256' or key.endswith('_sha256'):
                check_digest(item, location)
            else:
                invalid.extend(invalid_sha256_fields(item, location))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            invalid.extend(invalid_sha256_fields(item, f'{prefix}[{index}]'))
    return invalid


names = [n.decode('utf-8') for n in git('ls-files', '--cached', '-z').split(b'\0') if n]
image_hashes = {}
records = {}
for name in names:
    path = PurePosixPath(name)
    screenshot = len(path.parts) == 3 and path.parts[:2] == ('verification', 'screenshots') and re.fullmatch(r'[a-z0-9][a-z0-9-]*\.png', path.name)
    icon_png = name == 'assets/wkcii.png'
    icon_ico = name == 'assets/wkcii.ico'
    allowed = screenshot or icon_png or icon_ico or name in root_files or name in {'.github/FUNDING.yml', 'verification/rendering.json', 'verification/gallery.json', 'verification/installation.json', 'verification/movie-demux.json', 'verification/performance.json'} or (
        len(path.parts) == 2 and path.parts[0] in extensions and path.suffix in extensions[path.parts[0]])
    if not allowed:
        findings.append({'file': name, 'reason': 'outside publication allowlist'})
        continue
    data = git('show', ':' + name)
    total += len(data)
    if len(data) > (8_000_000 if screenshot or icon_png else 2_000_000):
        findings.append({'file': name, 'reason': 'unexpected source file size'})
    if screenshot or icon_png or icon_ico:
        reason = check_ico(data) if icon_ico else check_png(data)
        if reason:
            findings.append({'file': name, 'reason': reason})
        if screenshot:
            image_hashes[name] = hashlib.sha256(data).hexdigest()
        continue
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError:
        findings.append({'file': name, 'reason': 'non-text data'})
        continue
    if path.suffix == '.json':
        try:
            records[name] = json.loads(text)
        except json.JSONDecodeError:
            findings.append({'file': name, 'reason': 'invalid JSON'})
        else:
            for field in invalid_sha256_fields(records[name]):
                findings.append({'file': name, 'reason': 'invalid SHA-256 field: ' + field})
    for reason, pattern in patterns.items():
        if re.search(pattern, text):
            findings.append({'file': name, 'reason': reason})
for record_name, image_field, hash_field in (
        ('verification/gallery.json', 'image', 'png_sha256'),
        ('verification/rendering.json', 'file', 'sha256')):
    for capture in records.get(record_name, {}).get('captures', []):
        name = 'verification/' + capture.get(image_field, '')
        if image_hashes.get(name) != capture.get(hash_field) or name not in image_hashes:
            findings.append({'file': record_name, 'reason': 'capture does not match staged image: ' + name})
print(json.dumps({'staged_files': len(names), 'staged_bytes': total, 'findings': findings}, indent=2))
sys.exit(bool(findings))
