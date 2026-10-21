"""Install project-written parts of SPU image 20's render-command interpreter.

Each entry replaces one compiled function at a pinned boundary; the original
stays as the compiled reference the C++ declines to. Command handlers return
to the interpreter loop; the loop head (0x1308) and return path (0x1968) hand
the C++ loop's chosen next function to SPU_JUMP; a called routine returns
through its link like the compiled code. A routine's pin also covers every
lifted block its C++ replaces, so a change anywhere in it is caught. With
WKC_NATIVE_RENDER_VERIFY set, a routine runs both ways and compares them.
Only the identity of the generated blocks is public; their bytes stay in the
private generated tree. Blocks are hashed with SPU_JUMP spelled as the plain
trampoline, so the result does not depend on whether apply_spu_direct_jumps.py
has already run. A region installed by an earlier version is extended in place.
"""
import argparse
import hashlib
import re
from pathlib import Path

IMAGE = 'zz_raw_job_01294400'
BEGIN = '/* WKC_NATIVE_RENDER_COMMANDS_BEGIN */\n'
END = '/* WKC_NATIVE_RENDER_COMMANDS_END */\n'
# name: (entry address, pinned SHA-256 of the lifted blocks, kind, argument[, covered blocks])
ENTRIES = {
    'command 25': ('00001C70', '479056e95d8b442402a8d3d2826fda912dea0257ed94638cf7553eaf36e11132',
                   'handler', 'wkc_native_render_cmd25'),
    'loop head': ('00001308', '67945f39c0869dd4aac723cd2605ba34bc02d2fea70797c951041a51443dc0da', 'loop', 0),
    'loop return': ('00001968', 'a19b6a82f067470375764254e5cfba9ff5fd4c2d1244b31c04292edf25c31eed', 'loop', 1),
    'transform constants': ('0000A8E0', '2fb181c64f233ccdf36a21f23499196440060c33371db53447dbe468239b2b51', 'routine', 'wkc_native_render_transform_constants',
                            ('0000A950', '0000AB74', '0000AB94', '0000AB98', '0000ABB0', '0000AC20',
                             '0000ACF4', '0000AD2C', '0000AD34', '0000AD3C')),
    'table constants': ('000053C8', 'ef4e64a5ca15325b246997c30440ae97b11a9b95a9840da65532499638f03aab', 'routine', 'wkc_native_render_table_constants',
                        ('000053D8', '00005430', '00005460', '000054C0', '00005550', '00005908',
                         '00005980', '00005A28', '00005A40')),
    'state flush': ('0000AD50', '15aa08a27f713820e49bea28eb713f066ec30459037cd63d2f3bacf26b96bc56', 'routine', 'wkc_native_render_state_flush',
                    ('0000ADCC', '0000ADE0', '0000ADF0', '0000AE00', '0000AE40', '0000AE98', '0000AF48',
                     '0000AF9C', '0000AFB0', '0000B210', '0000B270', '0000B290', '0000B368', '0000B3D8',
                     '0000B410')),
    'draw': ('00009D58', 'cdd0b0f49181898ac247f0be331d3ce14f5a92857173789d16df8b0285274250', 'routine', 'wkc_native_render_draw',
             ('00009E60', '00009E90', '00009EA4', '00009EB8', '00009EF0', '00009FA4', '0000A004', '0000A018',
              '0000A0E0', '0000A168', '0000A2B8', '0000A3B8', '0000A3D8', '0000A4B0', '0000A4D0', '0000A520',
              '0000A570', '0000A630', '0000A670', '0000A7C0', '0000A810', '0000A89C')),
}
JUMP = re.compile(r'SPU_JUMP\(ctx, (0x[0-9A-F]+), ([A-Za-z_][A-Za-z0-9_]*)\);')


def canonical(block):
    return JUMP.sub(r'{ ctx->pc = \1; g_spu_trampoline_fn = \2; return; }', block)


def entry(address):
    return IMAGE + '_spu_func_' + address


def reference(address):
    return 'wkc_render_command_reference_' + address


def wrapper(address, kind, argument):
    if kind == 'routine':
        return (f'extern int {argument}(spu_context* ctx);\n'
                f'void {entry(address)}(spu_context* ctx) {{\n'
                '    if (wkc_native_render_verify_enabled()) {\n'
                f'        wkc_native_render_verify(ctx, {reference(address)}, {argument}, 0x{address}u);\n'
                '        SPU_RET(ctx);\n'
                '    }\n'
                f'    if (!{argument}(ctx)) {{ {reference(address)}(ctx); return; }}\n'
                '    SPU_RET(ctx);\n'
                '}\n')
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


def pin(block, covered):
    return hashlib.sha256((canonical(block) + ''.join(map(canonical, covered))).encode('utf-8')).hexdigest()


def covered_blocks(text, covers):
    return [text[slice(*function_block(text, 'void ' + entry(address)))] for address in covers]


def install(blocks, text, report=False):
    out = [BEGIN, 'typedef void (*wkc_spu_fn)(spu_context*);\n',
           'extern wkc_spu_fn wkc_native_render_loop(spu_context* ctx, int from_return);\n',
           'extern int wkc_native_render_verify_enabled(void);\n',
           'extern int wkc_native_render_verify(spu_context* ctx, void (*reference)(spu_context*),\n'
           '                                    int (*native)(spu_context*), unsigned entry);\n']
    for name, (address, expected, kind, argument, *covers) in ENTRIES.items():
        block = blocks[name]
        actual = pin(block, covered_blocks(text, covers[0] if covers else ()))
        if report: print(f'{name}: {actual}')
        elif actual != expected:
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


def apply(text, report=False):
    blocks, insert_at = {}, None
    if BEGIN in text:
        if text.count(BEGIN) != 1 or text.count(END) != 1:
            raise ValueError('Duplicate render-command hooks')
        start = text.index(BEGIN); stop = text.index(END, start) + len(END)
        region = text[start:stop]
        for name, (address, *_) in ENTRIES.items():
            ref = 'static void ' + reference(address)
            if ref + '(spu_context* ctx) {' in region:
                b, e = function_block(region, ref)
                blocks[name] = region[b:e].replace(ref, 'void ' + entry(address), 1)
        text, insert_at = text[:start] + text[stop:], start
    spans = []
    for name, (address, *_) in ENTRIES.items():
        if name not in blocks:
            b, e = function_block(text, 'void ' + entry(address))
            blocks[name] = text[b:e]; spans.append((b, e))
    for b, e in sorted(spans, reverse=True):
        text = text[:b] + text[e:]
        if insert_at is not None and b < insert_at: insert_at -= e - b
    if insert_at is None: insert_at = min(b for b, _ in spans)
    return text[:insert_at] + install(blocks, text, report) + text[insert_at:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generated_spu', type=Path)
    parser.add_argument('--print-pins', action='store_true', help='print each entry\'s hash; change nothing')
    args = parser.parse_args()
    path = args.generated_spu / IMAGE / 'spu_recomp.c'
    before = path.read_text(encoding='utf-8')
    if args.print_pins:
        apply(before, report=True); return
    after = apply(before)
    if before != after:
        path.write_text(after, encoding='utf-8', newline='\n')
    print('Native render commands: validated pinned blocks', ', '.join(ENTRIES))


if __name__ == '__main__':
    main()
