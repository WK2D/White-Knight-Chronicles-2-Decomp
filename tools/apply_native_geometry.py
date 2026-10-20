"""Install project-written blend, position and cofactor operations into the pinned private SPU lift.

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


def replacement_v2(block, expected=EXPECTED):
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


def replacement_v3(block, expected=EXPECTED):
    installed = replacement_v2(block, expected)
    # Precompute into private locals. Materialize each result at its original
    # architectural write, after any intervening stack stores that use the GPR.
    destination = {39:0, 27:1, 41:2, 40:3, 28:4, 42:5,
                   44:6, 30:7, 45:8, 43:9, 32:10, 46:11}
    marker = 'extern int wkc_native_blend_geometry'
    boundary = installed.index(marker)
    prefix, wrapper = installed[:boundary], installed[boundary:]
    wrapper = wrapper.replace('extern int wkc_native_transform_geometry(spu_context* ctx);\n',
        'extern int wkc_native_transform_geometry(spu_context* ctx);\n'
        'extern int wkc_native_cofactor_geometry(const spu_context* ctx, void* output, unsigned bytes);\n', 1)
    marker = '    if (!wkc_native_transform_geometry(ctx))'
    at = wrapper.index(marker)
    wrapper = wrapper[:at] + ('    u128 _wkc_cofactors[24];\n'
        '    const int _wkc_has_cofactors = wkc_native_cofactor_geometry(ctx, _wkc_cofactors, sizeof _wkc_cofactors);\n') + wrapper[at:]
    rewritten = set()
    def substitute(match):
        register = int(match[1])
        if register not in destination or register in rewritten:
            raise ValueError('Cofactor destination boundary changed')
        rewritten.add(register)
        return ('        ctx->gpr[' + str(register) + '] = _wkc_has_cofactors ? '
                '_wkc_cofactors[' + str(destination[register]) + '] : ' + match[2] + ';')
    wrapper = re.sub(r'^        ctx->gpr\[(\d+)\] = (spu_fms\([^\n]+\));$', substitute, wrapper, flags=re.M)
    if rewritten != set(destination):
        raise ValueError('Incomplete cofactor boundary')
    products = {50:12, 9:13, 3:14, 25:15, 8:16, 4:17,
                29:18, 7:19, 5:20, 31:21, 74:22, 2:23}
    rewritten_products = set()
    def substitute_product(match):
        register = int(match[1])
        if register not in products or register in rewritten_products:
            return match[0]
        rewritten_products.add(register)
        return ('        ctx->gpr[' + str(register) + '] = _wkc_has_cofactors ? '
                '_wkc_cofactors[' + str(products[register]) + '] : ' + match[2] + ';')
    wrapper = re.sub(r'^        ctx->gpr\[(\d+)\] = (spu_fm\([^\n]+\));$', substitute_product, wrapper, flags=re.M)
    if rewritten_products != set(products):
        raise ValueError('Incomplete cofactor product boundary')
    return prefix + wrapper


def replacement(block, expected=EXPECTED):
    # Keep the previous validated implementations as upgrade identities, while
    # the current supported path completes the boundary in project-written C++.
    installed = replacement_v3(block, expected)
    boundary = installed.index('extern int wkc_native_blend_geometry')
    wrapper = ('extern int wkc_native_blend_geometry(spu_context* ctx);\n'
               'extern int wkc_native_finish_geometry(spu_context* ctx);\n'
               'void ' + ENTRY + '(spu_context* ctx) {\n'
               '    if (!wkc_native_blend_geometry(ctx)) {' + REFERENCE + '(ctx);return;}\n'
               '    if (!wkc_native_finish_geometry(ctx)) {' + TAIL_REFERENCE + '(ctx);return;}\n'
               '    ctx->pc = ctx->gpr[15]._u32[0];\n'
               '    g_spu_trampoline_fn = spu_indirect_branch;\n'
               '}\n')
    return installed[:boundary] + wrapper + END


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
        if installed in (replacement_v1(block), replacement_v2(block), replacement_v3(block)):
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
