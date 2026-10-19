"""Lift WKC2 SPU images with validated title-specific indirect entry points.

The manifest contains hashes/addresses only. Game inputs and generated sources
remain private. Refuse a different image rather than applying stale boundaries.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import shutil
import tempfile

ENTRY_IMAGES = {
    'zz_raw_job_01272B80': ('e35c4f01ee691cb585987595edb49b44e85ab7822ebf0244b09dec4f852cdbdc',
                           (0x7D60, 0x7E38, 0x7E78)),
    'zz_raw_job_01283D80': ('2a7f2d82a4e290fb13c84a542d1c50b886e06a4ae2b34ceaf01f1aac31d3925e',
                           (0x49B8, 0x4AE0)),
}
BASE_IMAGES = ('spu_0000_at_01184E00', 'spu_0001_at_0118DB00',
               'spu_0002_at_011B2C10', 'spu_0003_at_011D3600', 'spu_0004_at_011ED2E0')
RAW_ADDRESSES = ('0121D800', '0122D500', '01230C00', '01235100', '01247100',
                 '0124F900', '01254600', '0125A700', '01260A00', '01262F80',
                 '0126DA00', '01272B80', '0127FB00', '01283D80', '01294400',
                 '012A1980', '012A2180', '012A6B00')
IMAGE_NAMES = (*BASE_IMAGES, *('zz_raw_job_' + addr for addr in RAW_ADDRESSES),
               'zzz_queue_job_0118FA00')
# These obsolete diagnostic wrappers duplicate jobs now reproducible directly
# from the executable. Keep their private files, but never register them twice
# or shift the queue job's diagnostic image ID from 24 to 30.
LEGACY_NAMES = {'zzz_asset_job_' + fingerprint for fingerprint in (
    '164B23483A68CBEA', '38DCC9863DD4E49F', '5708DE8168BA815C',
    '66E2457D0BEDAE24', '687D4BB18564813A', '6AEA19F8490A2656')}


def validated_entry_arguments(images):
    arguments = []
    for name, (expected, entries) in ENTRY_IMAGES.items():
        image = Path(images) / (name + '.elf')
        if not image.is_file() or hashlib.sha256(image.read_bytes()).hexdigest() != expected:
            raise ValueError('Title-specific SPU entry hints require the pinned baseline image')
        arguments += ['--extra-funcs', name + '=' + ','.join(hex(entry) for entry in entries)]
    return arguments


def selected_images(images):
    found = {path.stem for path in Path(images).glob('*.elf')}
    if set(IMAGE_NAMES) - found:
        raise ValueError('The reproducible 24-image SPU input set is incomplete')
    if found - set(IMAGE_NAMES) - LEGACY_NAMES:
        raise ValueError('Unreviewed SPU images require an explicit registry update')
    return [Path(images) / (name + '.elf') for name in IMAGE_NAMES]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--images', type=Path, required=True)
    parser.add_argument('--lifted', type=Path, required=True)
    args = parser.parse_args()
    hints = validated_entry_arguments(args.images)
    selected = selected_images(args.images)
    with tempfile.TemporaryDirectory(prefix='wkc2-spu-inputs-') as directory:
        for source in selected:
            shutil.copyfile(source, Path(directory) / source.name)
        command = [sys.executable, str(args.sdk / 'tools/build_spu_workloads.py'),
                   '--images', directory, '--lifted', str(args.lifted),
                   '--out', str(args.lifted / 'spu_workloads.c'),
                   '--register-fn', 'wkc2_spu_register_all', '--constructor',
                   '--title', 'wkc2', '--relift', *hints]
        subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
