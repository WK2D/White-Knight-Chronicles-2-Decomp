"""Install project-written matrix blending and position transforms into the pinned private SPU lift.

No generated game source is stored here. Refuse changed function bodies rather
than guessing a new boundary. The unchanged compiled body remains the fallback.
"""
import argparse
import hashlib
import re
from pathlib import Path

IMAGE = 'zz_raw_job_01283D80'
ENTRY = IMAGE + '_spu_func_0000CE08'
REFERENCE = 'wkc_geometry_compiled_reference_ce08'
TAIL_REFERENCE = 'wkc_geometry_compiled_tail_ce08'
EXPECTED = '712fbd652b754c8fba66c9d2619e21ed3fca0dca74790609fe650ca028e1883a'
TAIL = '        ctx->gpr[40] = spu_shufb(ctx->gpr[50], ctx->gpr[41], ctx->gpr[123]);'
BEGIN = '/* WKC_NATIVE_GEOMETRY_BEGIN */\n'
END = '/* WKC_NATIVE_GEOMETRY_END */\n'


def replacement_v1(block, expected=EXPECTED):
    if hashlib.sha256(block.encode('utf-8')).hexdigest() != expected:
        raise ValueError('Geometry block differs from the validated baseline lift')
    if block.count(TAIL) != 1:
        raise ValueError('Geometry tail boundary is ambiguous')
    original = block.replace('void ' + ENTRY, 'static void ' + REFERENCE, 1)
    wrapper = ('extern int wkc_native_blend_geometry(spu_context* ctx);\n'
               'void ' + ENTRY + '(spu_context* ctx) {\n'
               '    if (!wkc_native_blend_geometry(ctx)) {' + REFERENCE + '(ctx);return;}\n'
               + block[block.index(TAIL):])
    return BEGIN + original + '\n' + wrapper + END


def replacement(block, expected=EXPECTED):
    # Validate the original complete block, not just the rewritten fragment.
    replacement_v1(block, expected)
    tail = block[block.index(TAIL):]
    lines = tail.splitlines(True)
    position_fma = [line for line in lines if '= spu_fma(' in line]
    if len(position_fma) != 12:
        raise ValueError('Position transform boundary changed')
    native_tail = ''.join(line for line in lines if '= spu_fma(' not in line)
    original = block.replace('void ' + ENTRY, 'static void ' + REFERENCE, 1)
    tail_reference = 'static void ' + TAIL_REFERENCE + '(spu_context* ctx) {\n' + tail
    wrapper = ('extern int wkc_native_blend_geometry(spu_context* ctx);\n'
               'extern int wkc_native_transform_geometry(spu_context* ctx);\n'
               'void ' + ENTRY + '(spu_context* ctx) {\n'
               '    if (!wkc_native_blend_geometry(ctx)) {' + REFERENCE + '(ctx);return;}\n'
               '    if (!wkc_native_transform_geometry(ctx)) {' + TAIL_REFERENCE + '(ctx);return;}\n'
               + native_tail)
    return BEGIN + original + '\n' + tail_reference + '\n' + wrapper + END


def apply(text):
    if BEGIN in text:
        if text.count(BEGIN) != 1 or text.count(END) != 1:
            raise ValueError('Duplicate native geometry hook')
        start = text.index(BEGIN)
        stop = text.index(END, start) + len(END)
        ref = text.index('static void ' + REFERENCE, start)
        match = re.search(r'^}\n', text[ref:], re.M)
        if not match:
            raise ValueError('Missing compiled geometry reference boundary')
        block = text[ref:ref + match.end()].replace('static void ' + REFERENCE, 'void ' + ENTRY, 1)
        installed = text[start:stop]
        updated = replacement(block)
        if installed == replacement_v1(block):
            return text[:start] + updated + text[stop:]
        if installed != updated:
            raise ValueError('Installed geometry hook has changed')
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
    before = path.read_text(encoding='utf-8')
    after = apply(before)
    if args.check:
        if before != after:
            raise SystemExit('Native geometry hook is missing')
    elif after != before:
        path.write_text(after, encoding='utf-8', newline='\n')
    print('Native geometry hook: validated pinned body and compiled reference')


if __name__ == '__main__':
    main()
