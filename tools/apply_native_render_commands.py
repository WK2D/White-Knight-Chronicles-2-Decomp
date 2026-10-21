"""Install project-written render-command handlers in SPU image 20.

Each handler replaces one interpreter command's compiled entry at a pinned
boundary; the original stays as the compiled reference the C++ declines to.
Only the identity of the generated blocks is public. Their bytes stay in the
private generated tree. Blocks are hashed with SPU_JUMP spelled as the plain
trampoline, so the result does not depend on whether
apply_spu_direct_jumps.py has already run.
"""
import argparse
import hashlib
import re
from pathlib import Path

IMAGE = 'zz_raw_job_01294400'
LOOP = IMAGE + '_spu_func_00001968'
BEGIN = '/* WKC_NATIVE_RENDER_COMMANDS_BEGIN */\n'
END = '/* WKC_NATIVE_RENDER_COMMANDS_END */\n'
# command id: (handler entry, pinned SHA-256 of the lifted block, C++ function)
HANDLERS = {
    25: ('00001C70', '479056e95d8b442402a8d3d2826fda912dea0257ed94638cf7553eaf36e11132',
         'wkc_native_render_cmd25'),
}
JUMP = re.compile(r'SPU_JUMP\(ctx, (0x[0-9A-F]+), ([A-Za-z_][A-Za-z0-9_]*)\);')


def canonical(block):
    return JUMP.sub(r'{ ctx->pc = \1; g_spu_trampoline_fn = \2; return; }', block)


def entry(address):
    return IMAGE + '_spu_func_' + address


def reference(address):
    return 'wkc_render_command_reference_' + address


def install(blocks):
    """blocks: {command: original lifted block text}"""
    out = [BEGIN]
    for command, (address, expected, native) in sorted(HANDLERS.items()):
        block = blocks[command]
        if hashlib.sha256(canonical(block).encode('utf-8')).hexdigest() != expected:
            raise ValueError(f'Command {command} handler differs from the validated baseline lift')
        out.append(block.replace('void ' + entry(address), 'static void ' + reference(address), 1) + '\n')
        out.append(f'extern int {native}(spu_context* ctx);\n'
                   f'void {entry(address)}(spu_context* ctx) {{\n'
                   f'    if (!{native}(ctx)) {{ {reference(address)}(ctx); return; }}\n'
                   f'    SPU_JUMP(ctx, 0x1968, {LOOP});\n'
                   '}\n')
    out.append(END)
    return ''.join(out)


def function_block(text, name, start=0):
    begin = text.index(name + '(spu_context* ctx) {', start)
    begin = text.rindex('\n', 0, begin) + 1
    match = re.search(r'^}\n', text[begin:], re.M)
    return begin, begin + match.end()


def apply(text):
    if BEGIN in text:
        if text.count(BEGIN) != 1 or text.count(END) != 1:
            raise ValueError('Duplicate render-command hooks')
        start = text.index(BEGIN); stop = text.index(END, start) + len(END)
        blocks = {}
        for command, (address, _, _) in HANDLERS.items():
            b, e = function_block(text, 'static void ' + reference(address), start)
            blocks[command] = text[b:e].replace('static void ' + reference(address), 'void ' + entry(address), 1)
        if text[start:stop] != install(blocks):
            raise ValueError('Installed render-command hooks have changed')
        return text
    spans = {c: function_block(text, 'void ' + entry(a)) for c, (a, _, _) in HANDLERS.items()}
    blocks = {c: text[b:e] for c, (b, e) in spans.items()}
    for b, e in sorted(spans.values(), reverse=True):
        text = text[:b] + text[e:]
    first = min(b for b, _ in spans.values())
    return text[:first] + install(blocks) + text[first:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generated_spu', type=Path)
    args = parser.parse_args()
    path = args.generated_spu / IMAGE / 'spu_recomp.c'
    before = path.read_text(encoding='utf-8'); after = apply(before)
    if before != after:
        path.write_text(after, encoding='utf-8', newline='\n')
    print('Native render commands: validated pinned handlers', sorted(HANDLERS))


if __name__ == '__main__':
    main()
