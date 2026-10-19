"""Reject unknown/missing input before title-specific SPU generation."""
import importlib.util
from pathlib import Path
import tempfile

spec = importlib.util.spec_from_file_location('build_spu', Path(__file__).parents[1] / 'tools/build_spu.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    for name in module.ENTRY_IMAGES:
        for contents in (None, b'', b'unknown image', b'\x7fELF' + bytes(1024)):
            image = root / (name + '.elf')
            if contents is not None:
                image.write_bytes(contents)
            try:
                module.validated_entry_arguments(root)
            except ValueError:
                pass
            else:
                raise AssertionError('Missing or mismatched SPU image accepted')
    try:
        module.selected_images(root)
    except ValueError:
        pass
    else:
        raise AssertionError('Incomplete image registry accepted')
    for name in (*module.IMAGE_NAMES, *module.LEGACY_NAMES):
        (root / (name + '.elf')).touch()
    selected = module.selected_images(root)
    assert len(selected) == 24 and selected[-1].stem == 'zzz_queue_job_0118FA00'
    assert all(path.stem in module.IMAGE_NAMES for path in selected)
    (root / 'unreviewed.elf').touch()
    try:
        module.selected_images(root)
    except ValueError:
        pass
    else:
        raise AssertionError('Unreviewed image accepted into registry')
print('SPU generation refuses missing, empty and mismatched entry-hint inputs')
