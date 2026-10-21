"""Install project-written parts of SPU image 20's render-command interpreter.

Each entry replaces one compiled function at a pinned boundary; the original
stays as the compiled reference the C++ declines to. Command handlers return
to the interpreter loop; the loop head (0x1308) and return path (0x1968) hand
the C++ loop's chosen next function to SPU_JUMP. Only the identity of the
generated blocks is public; their bytes stay in the private generated tree.
Blocks are hashed with SPU_JUMP spelled as the plain trampoline, so the result
does not depend on whether apply_spu_direct_jumps.py has already run. A region
installed by an earlier version is extended in place.
"""
import argparse
import hashlib
import re
from pathlib import Path

IMAGE = 'zz_raw_job_01294400'
BEGIN = '/* WKC_NATIVE_RENDER_COMMANDS_BEGIN */\n'
END = '/* WKC_NATIVE_RENDER_COMMANDS_END */\n'
# name: (entry address, pinned SHA-256 of the lifted block, kind, argument)
ENTRIES = {
    'command 25': ('00001C70', '479056e95d8b442402a8d3d2826fda912dea0257ed94638cf7553eaf36e11132',
                   'handler', 'wkc_native_render_cmd25'),
    'loop head': ('00001308', '67945f39c0869dd4aac723cd2605ba34bc02d2fea70797c951041a51443dc0da', 'loop', 0),
    'loop return': ('00001968', 'a19b6a82f067470375764254e5cfba9ff5fd4c2d1244b31c04292edf25c31eed', 'loop', 1),
}
JUMP = re.compile(r'SPU_JUMP\(ctx, (0x[0-9A-F]+), ([A-Za-z_][A-Za-z0-9_]*)\);')


def canonical(block):
    return JUMP.sub(r'{ ctx->pc = \1; g_spu_trampoline_fn = \2; return; }', block)


def entry(address):
    return IMAGE + '_spu_func_' + address


def reference(address):
    return 'wkc_render_command_reference_' + address


def wrapper(address, kind, argument):
    if kind == 'handler':
        return (f'extern int {argument}(spu_context* ctx);\n'
                f'void {entry(address)}(spu_context* ctx) {{\n'
                f'    if (!{argument}(ctx)) {{ {reference(address)}(ctx); return; }}\n'
                f'    SPU_JUMP(ctx, 0x1968, {entry("00001968")});\n'
                '}\n')
    return (f'void {entry(address)}(spu_context* ctx) {{\n'
            f'    wkc_spu_fn next = wkc_native_render_loop(ctx, {argument});\n'
            f'    if (!next) {{ {reference(address)}(ctx); return; }}\n'
            '    SPU_JUMP(ctx, ctx->pc, next);\n'
            '}\n')


def install(blocks):
    out = [BEGIN, 'typedef void (*wkc_spu_fn)(spu_context*);\n',
           'extern wkc_spu_fn wkc_native_render_loop(spu_context* ctx, int from_return);\n']
    for name, (address, expected, kind, argument) in ENTRIES.items():
        block = blocks[name]
        if hashlib.sha256(canonical(block).encode('utf-8')).hexdigest() != expected:
            raise ValueError(f'{name} differs from the validated baseline lift')
        out.append(block.replace('void ' + entry(address), 'static void ' + reference(address), 1) + '\n')
        out.append(wrapper(address, kind, argument))
    out.append(END)
    return ''.join(out)


def function_block(text, name, start=0):
    begin = text.index(name + '(spu_context* ctx) {', start)
    begin = text.rindex('\n', 0, begin) + 1
    match = re.search(r'^}\n', text[begin:], re.M)
    return begin, begin + match.end()


def apply(text):
    blocks, insert_at = {}, None
    if BEGIN in text:
        if text.count(BEGIN) != 1 or text.count(END) != 1:
            raise ValueError('Duplicate render-command hooks')
        start = text.index(BEGIN); stop = text.index(END, start) + len(END)
        region = text[start:stop]
        for name, (address, _, _, _) in ENTRIES.items():
            ref = 'static void ' + reference(address)
            if ref + '(spu_context* ctx) {' in region:
                b, e = function_block(region, ref)
                blocks[name] = region[b:e].replace(ref, 'void ' + entry(address), 1)
        text, insert_at = text[:start] + text[stop:], start
    spans = []
    for name, (address, _, _, _) in ENTRIES.items():
        if name not in blocks:
            b, e = function_block(text, 'void ' + entry(address))
            blocks[name] = text[b:e]; spans.append((b, e))
    for b, e in sorted(spans, reverse=True):
        text = text[:b] + text[e:]
        if insert_at is not None and b < insert_at: insert_at -= e - b
    if insert_at is None: insert_at = min(b for b, _ in spans)
    return text[:insert_at] + install(blocks) + text[insert_at:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generated_spu', type=Path)
    args = parser.parse_args()
    path = args.generated_spu / IMAGE / 'spu_recomp.c'
    before = path.read_text(encoding='utf-8'); after = apply(before)
    if before != after:
        path.write_text(after, encoding='utf-8', newline='\n')
    print('Native render commands: validated pinned blocks', ', '.join(ENTRIES))


if __name__ == '__main__':
    main()
