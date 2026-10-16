"""Install opt-in guest update observations in private generated PPU sources."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('generated', type=Path)
args = parser.parse_args()

def install(address, declaration, hook, anchor=None):
    signature = f'void func_{address}(ppu_context* ctx) {{'
    for path in sorted(args.generated.glob('ppu_recomp_*.cpp')):
        text = path.read_text()
        start = text.find(signature)
        if start < 0:
            continue
        end = text.find('\nvoid func_', start + len(signature))
        if end < 0:
            end = len(text)
        body = text[start:end]
        if hook not in body:
            anchor = anchor or signature
            if body.count(anchor) != 1:
                raise SystemExit(f'Unexpected observation anchor in {address}')
            body = body.replace(anchor, anchor + '\n        ' + hook, 1)
            if declaration not in text:
                body = declaration + '\n' + body
            path.write_text(text[:start] + body + text[end:])
        print(f'Observation hook ready: {address}')
        return
    raise SystemExit(f'Expected BLUS30784 function {address} was not found')

install('009320D8', 'extern "C" void wkc2_observe_update(uint32_t app);',
        'wkc2_observe_update((uint32_t)ctx->gpr[3]);')
install('002F4570', 'extern "C" void wkc2_observe_startup(uint32_t object, uint32_t state_context);',
        'wkc2_observe_startup((uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[30]);',
        'ctx->lr = 0x002F4598; func_002E1D68(ctx); DRAIN_TRAMPOLINE(ctx);')
install('0006A0A8', 'extern "C" void wkc2_observe_content_request(ppu_context* ctx);',
        'wkc2_observe_content_request(ctx);')
install('0006CFA4', 'extern "C" void wkc2_observe_content_worker(ppu_context* ctx);',
        'wkc2_observe_content_worker(ctx);',
        'ctx->lr = 0x0006D0FC; func_0006C390(ctx); DRAIN_TRAMPOLINE(ctx);')

install('00AEF65C', 'extern "C" void wkc2_observe_graphics_wait(ppu_context* ctx);',
        'wkc2_observe_graphics_wait(ctx);',
        'if ((!((ctx->cr >> 0) & 4))) goto loc_00AEF6C0;')
