"""Install host synchronization hooks in private BLUS30784 v01.00 generated code."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('generated', type=Path)
args = parser.parse_args()
signature = 'void func_0006A1D4(ppu_context* ctx) {'
declaration = 'extern "C" void wkc2_wait_for_content_check(void);'
hook = '        wkc2_wait_for_content_check();'
for path in sorted(args.generated.glob('ppu_recomp_*.cpp')):
    text = path.read_text()
    if signature not in text:
        continue
    if hook not in text:
        text = text.replace(signature, declaration + '\n' + signature + '\n' + hook, 1)
        path.write_text(text)
    print('Content installation synchronization hook ready')
    break
else:
    raise SystemExit('Expected BLUS30784 installer request function was not found')
