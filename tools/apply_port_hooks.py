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
    text = path.read_text(encoding='latin-1')
    if signature not in text:
        continue
    if hook not in text:
        text = text.replace(signature, declaration + '\n' + signature + '\n' + hook, 1)
        path.write_text(text,encoding='latin-1')
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
    text = path.read_text(encoding='latin-1')
    if step_signature not in text:
        continue
    if 'func_00325248_original' not in text:
        text = text.replace(step_signature, step_wrapper, 1)
        path.write_text(text,encoding='latin-1')
    print('Boot sequence step hook ready')
    break
else:
    raise SystemExit('Expected BLUS30784 boot sequencer function was not found')

# The eight-slot native System submenu reuses the original widgets and input.
# Generated code and proprietary menu resources remain private.
menu_hooks = {
    '0065D798': '''extern "C" void wkc2_graphics_menu_rows(ppu_context*, uint32_t);
static void func_0065D798_original(ppu_context*);
void func_0065D798(ppu_context* ctx) {
    const uint32_t object=(uint32_t)ctx->gpr[3];
    func_0065D798_original(ctx);
    wkc2_graphics_menu_rows(ctx,object);
}
static void func_0065D798_original(ppu_context* ctx) {''',
    '00665628': '''extern "C" void wkc2_graphics_menu_labels(ppu_context*, uint32_t, uint32_t);
static void func_00665628_original(ppu_context*);
void func_00665628(ppu_context* ctx) {
    const uint32_t object=(uint32_t)ctx->gpr[3],render=(uint32_t)ctx->gpr[5];
    func_00665628_original(ctx);
    wkc2_graphics_menu_labels(ctx,object,render);
}
static void func_00665628_original(ppu_context* ctx) {''',
    '0065F20C': '''extern "C" void wkc2_graphics_menu_labels(ppu_context*, uint32_t, uint32_t);
static void func_0065F20C_original(ppu_context*);
void func_0065F20C(ppu_context* ctx) {
    const uint32_t object=(uint32_t)ctx->gpr[3],render=(uint32_t)ctx->gpr[5];
    func_0065F20C_original(ctx);
    wkc2_graphics_menu_labels(ctx,object,render);
}
static void func_0065F20C_original(ppu_context* ctx) {''',
    '0065E300': '''extern "C" int wkc2_graphics_menu_finish(ppu_context*);
static void func_0065E300_original(ppu_context*);
void func_0065E300(ppu_context* ctx) {
    if (!wkc2_graphics_menu_finish(ctx)) func_0065E300_original(ctx);
}
static void func_0065E300_original(ppu_context* ctx) {''',
}
for address, wrapper in menu_hooks.items():
    signature = f'void func_{address}(ppu_context* ctx) {{'
    for path in sorted(args.generated.glob('ppu_recomp_*.cpp')):
        text = path.read_text(encoding='latin-1')
        if signature not in text:
            continue
        if f'func_{address}_original' not in text:
            path.write_text(text.replace(signature,wrapper,1),encoding='latin-1')
        print(f'Graphics menu hook ready: {address}')
        break
    else:
        raise SystemExit(f'Expected BLUS30784 menu function {address} not found')

# The game's two-vblank interval is retained for fixed caps. Unlocked mode
# bypasses only its wait, while the host services actual completed frames.
for address, callback in [('00AEDC7C','wkc2_graphics_interval_requested'), ('00AEF2E8','wkc2_graphics_frame_wait')]:
    signature=f'void func_{address}(ppu_context* ctx) {{'
    wrapper=f"""extern "C" void {callback}(ppu_context*);
static void func_{address}_original(ppu_context*);
void func_{address}(ppu_context* ctx) {{
    {callback}(ctx);
    func_{address}_original(ctx);
}}
static void func_{address}_original(ppu_context* ctx) {{"""
    for path in sorted(args.generated.glob('ppu_recomp_*.cpp')):
        text=path.read_text(encoding='latin-1')
        if signature not in text: continue
        if f'func_{address}_original' not in text:
            path.write_text(text.replace(signature,wrapper,1),encoding='latin-1')
        print(f'Graphics timing hook ready: {address}')
        break
    else: raise SystemExit(f'Expected BLUS30784 timing function {address} not found')
