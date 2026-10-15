"""Install opt-in guest update observations in private generated PPU sources."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('generated', type=Path)
args = parser.parse_args()
signature = 'void func_009320D8(ppu_context* ctx) {'
declaration = 'extern "C" void wkc2_observe_update(uint32_t app);'
hook = '        wkc2_observe_update((uint32_t)ctx->gpr[3]);'
for path in sorted(args.generated.glob('ppu_recomp_*.cpp')):
    text = path.read_text()
    if signature not in text:
        continue
    if hook not in text:
        text = text.replace(signature, declaration + '\n' + signature + '\n' + hook, 1)
        path.write_text(text)
    print('Update observation hook ready')
    break
else:
    raise SystemExit('Expected BLUS30784 update function was not found')
