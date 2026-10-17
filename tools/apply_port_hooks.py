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

# Optional intro skip: wrap the boot sequencer's step advance so the host can
# remap the chosen step (see wkc2_boot_step_remap, inert unless enabled).
step_signature = 'void func_00325248(ppu_context* ctx) {'
step_wrapper = '''extern "C" int32_t wkc2_boot_step_remap(int32_t);
static void func_00325248_original(ppu_context* ctx);
void func_00325248(ppu_context* ctx) {
        const uint32_t sequencer = (uint32_t)ctx->gpr[3];
        func_00325248_original(ctx);
        const int32_t step = (int32_t)vm_read32(sequencer + 0x50);
        const int32_t mapped = wkc2_boot_step_remap(step);
        if (mapped != step) vm_write32(sequencer + 0x50, (uint32_t)mapped);
}
static void func_00325248_original(ppu_context* ctx) {'''
for path in sorted(args.generated.glob('ppu_recomp_*.cpp')):
    text = path.read_text()
    if step_signature not in text:
        continue
    if 'func_00325248_original' not in text:
        text = text.replace(step_signature, step_wrapper, 1)
        path.write_text(text)
    print('Boot sequence step hook ready')
    break
else:
    raise SystemExit('Expected BLUS30784 boot sequencer function was not found')
