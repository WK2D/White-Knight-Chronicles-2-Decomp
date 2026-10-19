"""The title hook must reject stale generated bodies and edited installations."""
import hashlib
import importlib.util
from pathlib import Path
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('hook',Path(__file__).with_name('apply_native_geometry.py'))
hook=importlib.util.module_from_spec(spec);spec.loader.exec_module(hook)
block=('void '+hook.ENTRY+'(spu_context* ctx) {\n'
       '    /* Synthetic test-only prefix; no game code. */\n'+hook.TAIL+'\n}\n')
text=block+'\nvoid synthetic_next(spu_context* ctx) {}\n'
try:hook.apply(text)
except ValueError:pass
else:raise AssertionError('Unrecognized generated body accepted')
replace=hook.replacement
digest=hashlib.sha256(block.encode()).hexdigest()
with patch.object(hook,'replacement',lambda value:replace(value,digest)):
    modified=hook.apply(text)
    assert hook.apply(modified)==modified
    assert hook.REFERENCE in modified and 'wkc_native_blend_geometry' in modified
    for corrupt in (modified.replace('return;}','return; }'),modified+hook.BEGIN,modified.replace(hook.END,'')):
        try:hook.apply(corrupt)
        except ValueError:pass
        else:raise AssertionError('Edited or duplicate hook accepted')
print('Native geometry hook rejects unknown bodies and edited installations; reapplication is a no-op')
