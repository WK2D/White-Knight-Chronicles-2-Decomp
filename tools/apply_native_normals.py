"""Install the project-written final-normal batch at a pinned private boundary.

Only the identity of the generated block is public. Its bytes and the compiled
reference remain in the user's private generated tree.
"""
import argparse
import hashlib
import re
from pathlib import Path

IMAGE = 'zz_raw_job_01283D80'
ENTRY = IMAGE + '_spu_func_0000D500'
NEXT = IMAGE + '_spu_func_0000D874'
REFERENCE = 'wkc_normals_compiled_reference_d500'
EXPECTED = 'bd7c73abae3f70d47d74ded635b83449496394cf6e8c60ffabe97155cb56716f'
BEGIN = '/* WKC_NATIVE_NORMALS_BEGIN */\n'
END = '/* WKC_NATIVE_NORMALS_END */\n'


def replacement(block, expected=EXPECTED):
    if hashlib.sha256(block.encode('utf-8')).hexdigest() != expected:
        raise ValueError('Final-normal block differs from the validated baseline lift')
    original = block.replace('void ' + ENTRY, 'static void ' + REFERENCE, 1)
    wrapper = ('extern int wkc_native_finish_normals(spu_context* ctx);\n'
               'void ' + ENTRY + '(spu_context* ctx) {\n'
               '    if (!wkc_native_finish_normals(ctx)) {' + REFERENCE + '(ctx);return;}\n'
               '    ctx->pc = 0xD874;\n'
               '    g_spu_trampoline_fn = ' + NEXT + ';\n'
               '}\n')
    return BEGIN + original + '\n' + wrapper + END


def apply(text):
    if BEGIN in text:
        if text.count(BEGIN) != 1 or text.count(END) != 1:
            raise ValueError('Duplicate native normal hook')
        start = text.index(BEGIN);stop = text.index(END, start) + len(END)
        ref = text.index('static void ' + REFERENCE, start)
        match = re.search(r'^}\n', text[ref:], re.M)
        if not match:
            raise ValueError('Missing compiled normal reference boundary')
        block = text[ref:ref + match.end()].replace('static void ' + REFERENCE, 'void ' + ENTRY, 1)
        if text[start:stop] != replacement(block):
            raise ValueError('Installed normal hook has changed')
        return text
    start = text.index('void ' + ENTRY + '(spu_context* ctx) {')
    stop = text.index('\nvoid ', start + 1)
    return text[:start] + replacement(text[start:stop]) + text[stop:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generated_spu', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    path = args.generated_spu / IMAGE / 'spu_recomp.c'
    before = path.read_text(encoding='utf-8');after = apply(before)
    if args.check:
        if before != after:
            raise SystemExit('Native normal hook is missing')
    elif before != after:
        path.write_text(after, encoding='utf-8', newline='\n')
    print('Native normal hook: validated pinned body and compiled reference')


if __name__ == '__main__':
    main()
