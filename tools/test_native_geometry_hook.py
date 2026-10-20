"""The title hook must reject stale generated bodies and edited installations."""
import hashlib
import importlib.util
from pathlib import Path
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('hook',Path(__file__).with_name('apply_native_geometry.py'))
hook=importlib.util.module_from_spec(spec);spec.loader.exec_module(hook)
block=('void '+hook.ENTRY+'(spu_context* ctx) {\n'
       '    /* Synthetic test-only prefix; no game code. */\n'+hook.TAIL+'\n'
       + ''.join('        ctx->gpr[0] = spu_fma(a,b,c);\n' for _ in range(12))
       + ''.join('        ctx->gpr['+str(reg)+'] = spu_fm(a,b);\n' for reg in (50,9,3,25,8,4,29,7,5,31,74,2))
       + ''.join('        /* synthetic write boundary */\n        ctx->gpr['+str(reg)+'] = spu_fms(a,b,c);\n'
                 for reg in (39,40,44,43,41,42,45,30,32,46,27,28))+'}\n')
text=block+'\nvoid synthetic_next(spu_context* ctx) {}\n'
try:hook.apply(text)
except ValueError:pass
else:raise AssertionError('Unrecognized generated body accepted')
replace=hook.replacement
replace_v1=hook.replacement_v1
replace_v2=hook.replacement_v2
replace_v3=hook.replacement_v3
digest=hashlib.sha256(block.encode()).hexdigest()
with patch.object(hook,'replacement',lambda value:replace(value,digest)), \
     patch.object(hook,'replacement_v1',lambda value,expected=digest:replace_v1(value,digest)):
    modified=hook.apply(text)
    assert hook.apply(modified)==modified
    assert hook.REFERENCE in modified and 'wkc_native_finish_geometry' in modified
    legacy=replace_v1(block,digest)+'\nvoid synthetic_next(spu_context* ctx) {}\n'
    assert hook.apply(legacy)==modified
    legacy=replace_v2(block,digest)+'\nvoid synthetic_next(spu_context* ctx) {}\n'
    assert hook.apply(legacy)==modified
    legacy=replace_v3(block,digest)+'\nvoid synthetic_next(spu_context* ctx) {}\n'
    assert hook.apply(legacy)==modified
    wrapper=modified[modified.index('extern int wkc_native_blend_geometry'):]
    assert 'spu_fm' not in wrapper and '_wkc_cofactors[' not in wrapper
    assert hook.TAIL_REFERENCE+'(ctx)' in wrapper and 'g_spu_trampoline_fn' in wrapper
    for corrupt in (modified.replace('return;}','return; }'),modified+hook.BEGIN,modified.replace(hook.END,'')):
        try:hook.apply(corrupt)
        except ValueError:pass
        else:raise AssertionError('Edited or duplicate hook accepted')
print('Native geometry hook rejects unknown bodies and edited installations; reapplication is a no-op')

spec=importlib.util.spec_from_file_location('normal_hook',Path(__file__).with_name('apply_native_normals.py'))
normals=importlib.util.module_from_spec(spec);spec.loader.exec_module(normals)
block=('void '+normals.ENTRY+'(spu_context* ctx) {\n'
       '    /* Synthetic normal boundary only; no game code. */\n}\n')
text=block+'\nvoid synthetic_next(spu_context* ctx) {}\n'
try:normals.apply(text)
except ValueError:pass
else:raise AssertionError('Unrecognized normal body accepted')
normal_replacement=normals.replacement;digest=hashlib.sha256(block.encode()).hexdigest()
with patch.object(normals,'replacement',lambda value:normal_replacement(value,digest)):
    modified=normals.apply(text)
    assert normals.apply(modified)==modified
    assert 'wkc_native_finish_normals(ctx)' in modified and normals.REFERENCE+'(ctx)' in modified
    for corrupt in (modified.replace('0xD874','0xD875'),modified+normals.BEGIN,modified.replace(normals.END,'')):
        try:normals.apply(corrupt)
        except ValueError:pass
        else:raise AssertionError('Edited or duplicate normal hook accepted')
print('Native normal hook rejects unknown bodies and edited installations; reapplication is a no-op')
