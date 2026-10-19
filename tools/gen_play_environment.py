"""Compile the runtime's environment names into the clean-play guard.

Only names are emitted. Paths, values, game files and user environment data are
never read. The player executable does not run this build-time tool.
"""
import argparse
import json
from pathlib import Path
import re

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--project',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
args=p.parse_args()
names=set()
paths=[*args.project.glob('*.cpp'),*args.project.glob('*.h')]
for subtree in ('runtime','libs'):
    paths += [path for path in (args.sdk/subtree).rglob('*') if path.suffix in ('.c','.cpp','.h','.hpp')]
for path in paths:
    names.update(re.findall(r'(?:getenv|GetEnvironmentVariableA)\(\s*"([A-Za-z0-9_]+)"',path.read_text(encoding='utf-8',errors='replace')))
names -= {'USER','USERNAME'}
text='// Generated environment NAMES only; no values or local paths.\n'
text+='static constexpr const char* runtime_environment_names[] = {\n'
text+=''.join('    '+json.dumps(name)+',\n' for name in sorted(names))+'};\n'
args.out.parent.mkdir(parents=True,exist_ok=True)
if not args.out.exists() or args.out.read_text(encoding='utf-8')!=text:
    args.out.write_text(text,encoding='utf-8',newline='\n')
print('Clean-play guard: '+str(len(names))+' runtime environment names')
