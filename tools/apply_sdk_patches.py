"""Apply or upgrade a verified prefix of the pinned SDK patch stack."""
import argparse
import hashlib
import re
import subprocess
import tempfile
from pathlib import Path, PurePosixPath


def git(directory, *args):
    return subprocess.run(['git', '-C', str(directory), *args],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def apply_stack(sdk, patch_directory, expected_commit):
    sdk = Path(sdk).resolve()
    revision = git(sdk, 'rev-parse', 'HEAD')
    if revision.returncode or revision.stdout.decode().strip() != expected_commit:
        raise RuntimeError('SDK revision mismatch')
    patches = sorted(Path(patch_directory).resolve().glob('*.patch'))
    if not patches:
        raise RuntimeError('No SDK patches found')
    names = set()
    for patch in patches:
        pairs = re.findall(r'^diff --git a/(.*?) b/(.*?)$',
                           patch.read_text(encoding='utf-8'), re.M)
        if not pairs:
            raise RuntimeError('Patch has no file headers: ' + patch.name)
        for old, new in pairs:
            path = PurePosixPath(new)
            if old != new or path.is_absolute() or '..' in path.parts or ':' in new or '.git' in path.parts:
                raise RuntimeError('Unsupported SDK patch path')
            names.add(new)
    names = sorted(names)

    def read(path):
        return path.read_bytes() if path.exists() else None

    def digest(data):
        return hashlib.sha256(data.replace(b'\r\n', b'\n')).digest() if data is not None else None

    original = {name: read(sdk / name) for name in names}
    current = [digest(original[name]) for name in names]
    with tempfile.TemporaryDirectory(prefix='wkc2-sdk-patches-') as temporary:
        scratch = Path(temporary)
        for name in names:
            result = git(sdk, 'show', expected_commit + ':' + name)
            if result.returncode == 0:
                target = scratch / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(result.stdout)
        known_prefix = None
        for count in range(len(patches) + 1):
            if [digest(read(scratch / name)) for name in names] == current:
                known_prefix = count
            if count < len(patches):
                # Reconstruct from pinned bytes independently of the user's Git
                # line-ending policy. Upstream has mixed CRLF/LF context; only
                # context whitespace is flexible here. Known-prefix hashes below
                # still reject unrelated SDK edits, including indentation changes.
                result = git(scratch, '-c', 'core.autocrlf=false', 'apply',
                             '--ignore-space-change', str(patches[count]))
                if result.returncode:
                    raise RuntimeError('Cannot reconstruct SDK patch: ' + patches[count].name)
        if known_prefix is None:
            raise RuntimeError('SDK files differ from every known patch prefix; local edits preserved')
        desired = {name: read(scratch / name) for name in names}
        if any(value is None for value in desired.values()):
            raise RuntimeError('SDK file deletion is unsupported')
        if any(read(sdk / name) != original[name] for name in names):
            raise RuntimeError('SDK changed during patch validation; retry after edits finish')
        changed = 0
        for name in names:
            if digest(original[name]) == digest(desired[name]):
                continue
            target = sdk / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(desired[name])
            changed += 1
    print(f'SDK patch stack: {len(patches)} patches, prefix {known_prefix}, {changed} files updated')
    return changed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--patches', required=True, type=Path)
    parser.add_argument('--expected-commit', required=True)
    options = parser.parse_args()
    try:
        apply_stack(options.sdk, options.patches, options.expected_commit)
    except RuntimeError as error:
        parser.exit(1, str(error) + '\n')
