"""Keep the current SDK patch and regression-check counts consistent across the docs.

    python tools/sync_status_counts.py          # update the docs
    python tools/sync_status_counts.py --check  # exit 1 if any count disagrees

The patch count is the number of numbered files in patches/. The check count is
project.json's current_build.runtime_checks_passed. Only the sentences below,
which state the *current* status, are changed; dated measurements elsewhere keep
the counts they were recorded with. Add a pattern here when a new page states the
current counts.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# (file, pattern with named groups `patches` and/or `checks`)
SITES = [
    ('README.md', r'\*\*(?P<checks>\d+) Windows regression checks\*\*'),
    ('docs/BUILD.md', r'the stack contains (?P<patches>\d+) SDK patches\. All\s+(?P<checks>\d+) Windows checks'),
    ('docs/BUILD.md', r'The current source stack contains (?P<patches>\d+) numbered SDK patches, and all (?P<checks>\d+) checks'),
    ('docs/CONTRIBUTING.md', r'passes (?P<checks>\d+) checks with (?P<patches>\d+) SDK patches'),
    ('docs/GALLERY.md', r'Current source: (?P<patches>\d+) SDK patches / (?P<checks>\d+) Windows checks'),
    ('docs/FINDINGS.md', r'\*\*Current source checkpoint:\*\* (?P<patches>\d+) SDK patches / (?P<checks>\d+) Windows checks'),
]

# These are current-state descriptions in project.json, not dated measurements.
JSON_TEXT_SITES = [
    (('progress_reporting', 'current_milestone'),
     r'(?P<patches>\d+) SDK patches / (?P<checks>\d+) Windows checks'),
    (('native_graphics_settings', 'scope'),
     r'Current source has (?P<patches>\d+) SDK patches / (?P<checks>\d+) passing Windows checks'),
    (('native_save_load_scope',), r'current (?P<patches>\d+)-patch Release build'),
]


def current():
    patches = len(list((ROOT / 'patches').glob('[0-9][0-9][0-9][0-9]-*.patch')))
    project = json.loads((ROOT / 'project.json').read_text(encoding='utf-8'))
    return {'patches': patches, 'checks': project['current_build']['runtime_checks_passed']}


def main():
    want = current()
    check = '--check' in sys.argv
    stale = []
    def fix_ordered(match):
        out = match.group(0)
        spans = sorted(((match.span(k), k) for k in ('patches', 'checks')
                        if k in match.groupdict() and match.group(k) is not None), reverse=True)
        for (s, e), k in spans:
            s -= match.start(); e -= match.start()
            out = out[:s] + str(want[k]) + out[e:]
        return out

    for name, pattern in SITES:
        path = ROOT / name
        text = path.read_text(encoding='utf-8')
        m = re.search(pattern, text)
        if not m:
            stale.append(f'{name}: status sentence not found (update tools/sync_status_counts.py)')
            continue

        updated = re.sub(pattern, fix_ordered, text, count=1)
        if updated != text:
            stale.append(f'{name}: {m.group(0)!r}')
            if not check:
                path.write_text(updated, encoding='utf-8', newline='\n')

    path = ROOT / 'project.json'
    project = json.loads(path.read_text(encoding='utf-8'))
    json_changed = False
    for keys, pattern in JSON_TEXT_SITES:
        parent = project
        for key in keys[:-1]:
            parent = parent[key]
        text = parent[keys[-1]]
        match = re.search(pattern, text)
        label = 'project.json:' + '.'.join(keys)
        if not match:
            stale.append(label + ': current status sentence not found')
            continue
        updated = re.sub(pattern, fix_ordered, text, count=1)
        if updated != text:
            stale.append(f'{label}: {match.group(0)!r}')
            parent[keys[-1]] = updated
            json_changed = True
    if json_changed and not check:
        path.write_text(json.dumps(project, indent=2) + '\n', encoding='utf-8', newline='\n')
    if check:
        if stale:
            print(f'Status counts differ from {want}:')
            print('\n'.join('  ' + s for s in stale))
            return 1
        print(f'Status counts match: {want["patches"]} patches, {want["checks"]} checks')
        return 0
    print('\n'.join(f'updated {s}' for s in stale) or f'already current: {want}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
