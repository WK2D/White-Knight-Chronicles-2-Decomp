"""Wrap newly observed asset-backed SPU jobs for the next static lift.

Inputs are private dumps produced by SPU_DUMP_MISS during a native probe.
Content fingerprints are checked before import. Existing registered jobs are
skipped; this does not ship game bytes or implement an interpreter fallback.
"""
import argparse
from pathlib import Path
import re
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
p.add_argument('--misses', type=Path, required=True)
p.add_argument('--registry', type=Path, required=True)
p.add_argument('--images', type=Path, required=True)
a = p.parse_args()
sys.path.insert(0, str(a.sdk.resolve()/'tools'))
from wrap_spu_elf import wrap

known = {int(s,16) for s in re.findall(r'register_img\(0x([0-9A-F]+)ULL',a.registry.read_text())}
a.images.mkdir(parents=True,exist_ok=True)
for path in sorted(a.misses.glob('spujob_*.bin')):
    match = re.fullmatch(r'spujob_([0-9A-F]{16})_(\d+)\.bin',path.name)
    if not match: continue
    expected = int(match[1],16)
    if expected in known: continue
    data = path.read_bytes()
    h = 1469598103934665603  # SDK registry's FNV-1a offset basis.
    for byte in data: h = ((h ^ byte)*1099511628211) & ((1<<64)-1)
    if len(data) != int(match[2]) or h != expected:
        raise ValueError(f'Corrupt SPU dump: {path}')
    if not any(data):
        print(f'{path.name}: empty image rejected; investigate job descriptor/source memory')
        continue
    out = a.images/f'zzz_asset_job_{h:016X}.elf'
    out.write_bytes(wrap(data,base=0,entry=0))
    print(f'{out.name}: {len(data)} raw bytes')
