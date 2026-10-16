"""Check the staged publication snapshot before publishing. Prints no file contents."""
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
    '.gitignore', '.gitattributes', 'README.md', 'LICENSE', 'LICENSE.ps3recomp', 'CMakeLists.txt',
    'project.json', 'decompression-verification.json', 'main.cpp', 'stubs.cpp',
    'thread_diagnostics.cpp', 'graphics_diagnostics.cpp', 'windows_guids.c', 'lwcond.cpp', 'lfqueue.cpp', 'pamf.cpp', 'mpegps.hpp', 'dmux.cpp', 'vpost.cpp', 'adec.cpp', 'vdec.cpp',
}
extensions = {'tools': {'.py', '.ps1', '.java'}, 'tests': {'.cpp', '.c'},
              'patches': {'.patch'}, 'docs': {'.md'}}
patterns = {
    'personal filesystem path': r'(?:[A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/][^\s]+|/(?:Users|home)/[^\s]+)',
    'credential': r'(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9_-]{20,}|AKIA[0-9A-Z]{16}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----)',
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

names = [n.decode('utf-8') for n in git('ls-files', '--cached', '-z').split(b'\0') if n]
for name in names:
    path = PurePosixPath(name)
    screenshot = len(path.parts) == 3 and path.parts[:2] == ('verification', 'screenshots') and re.fullmatch(r'[a-z0-9][a-z0-9-]*\.png', path.name)
    allowed = screenshot or name in root_files or name in {'.github/FUNDING.yml', 'verification/rendering.json', 'verification/gallery.json', 'verification/installation.json', 'verification/movie-demux.json'} or (
        len(path.parts) == 2 and path.parts[0] in extensions and path.suffix in extensions[path.parts[0]])
    if not allowed:
        findings.append({'file': name, 'reason': 'outside publication allowlist'})
        continue
    data = git('show', ':' + name)
    total += len(data)
    if len(data) > 2_000_000:
        findings.append({'file': name, 'reason': 'unexpected source file size'})
    if screenshot:
        reason = check_png(data)
        if reason:
            findings.append({'file': name, 'reason': reason})
        continue
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError:
        findings.append({'file': name, 'reason': 'non-text data'})
        continue
    for reason, pattern in patterns.items():
        if re.search(pattern, text):
            findings.append({'file': name, 'reason': reason})
print(json.dumps({'staged_files': len(names), 'staged_bytes': total, 'findings': findings}, indent=2))
sys.exit(bool(findings))
