"""Check the staged source snapshot before publishing. Prints no file contents."""
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]

def git(*args):
    return subprocess.check_output(['git', '-C', str(root), *args])

root_files = {
    '.gitignore', '.gitattributes', 'README.md', 'LICENSE', 'LICENSE.ps3recomp', 'CMakeLists.txt',
    'project.json', 'decompression-verification.json', 'main.cpp', 'stubs.cpp',
    'thread_diagnostics.cpp', 'windows_guids.c', 'lwcond.cpp', 'lfqueue.cpp',
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
names = [n.decode('utf-8') for n in git('ls-files', '--cached', '-z').split(b'\0') if n]
for name in names:
    path = PurePosixPath(name)
    allowed = name in root_files or name == 'verification/rendering.json' or (
        len(path.parts) == 2 and path.parts[0] in extensions and path.suffix in extensions[path.parts[0]])
    if not allowed:
        findings.append({'file': name, 'reason': 'outside publication allowlist'})
        continue
    data = git('show', ':' + name)
    total += len(data)
    if len(data) > 2_000_000:
        findings.append({'file': name, 'reason': 'unexpected source file size'})
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
