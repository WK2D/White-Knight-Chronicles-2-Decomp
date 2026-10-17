"""Extract the SDK's actual recursion guard for its isolated regression."""
from pathlib import Path
import re
import sys

sdk, output = map(Path, sys.argv[1:3])
source = (sdk / 'runtime/spu/spu_drain.c').read_text(encoding='utf-8')
# Top-level function ends at an unindented brace. Fail rather than silently
# testing a stale copied implementation if its declaration or layout changes.
matches = re.findall(
    r'^void spu_depth_guard\(spu_context\* ctx\)\n\{.*?^\}',
    source, flags=re.MULTILINE | re.DOTALL)
if len(matches) != 1:
    raise SystemExit('Expected exactly one SDK spu_depth_guard definition')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(matches[0] + '\n', encoding='utf-8', newline='\n')
