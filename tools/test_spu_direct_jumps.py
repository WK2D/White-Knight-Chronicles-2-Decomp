"""Synthetic check of apply_spu_direct_jumps: rewrites, skips pinned hooks, idempotent."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from apply_spu_direct_jumps import apply  # noqa: E402

source = '''void img_spu_func_00000010(spu_context* ctx) {
        if (ctx->gpr[3]._u32[0] == 0) { ctx->pc = 0x40; g_spu_trampoline_fn = img_spu_func_00000040; return; }
        { ctx->pc = 0x20; g_spu_trampoline_fn = img_spu_func_00000020; return; }
}
/* WKC_NATIVE_TEST_BEGIN */
static void reference(spu_context* ctx) {
        { ctx->pc = 0x30; g_spu_trampoline_fn = img_spu_func_00000030; return; }
}
/* WKC_NATIVE_TEST_END */
void img_spu_func_00000020(spu_context* ctx) {
        ctx->pc = ctx->gpr[0]._u32[0]; g_spu_trampoline_fn = spu_indirect_branch; return;
}
'''
once = apply(source)
assert 'if (ctx->gpr[3]._u32[0] == 0) SPU_JUMP(ctx, 0x40, img_spu_func_00000040);' in once, once
assert '        SPU_JUMP(ctx, 0x20, img_spu_func_00000020);\n' in once, once
# A pinned hook region keeps the exact lifted text its hash describes.
assert '{ ctx->pc = 0x30; g_spu_trampoline_fn = img_spu_func_00000030; return; }' in once, once
# Indirect transfers are not direct jumps.
assert 'g_spu_trampoline_fn = spu_indirect_branch; return;' in once, once
assert once.count('SPU_JUMP(') == 2, once
assert apply(once) == once
print('SPU direct-jump rewrite: 2 sites, pinned region and indirect branch untouched')
