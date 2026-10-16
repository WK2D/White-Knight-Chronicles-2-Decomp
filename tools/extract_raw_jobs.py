"""Extract observed raw SPURS job binaries from the pinned WKC2 executable.

These lack ELF headers, so the SDK's ELF scanner cannot discover them. The
addresses and lengths below come from actual job descriptors in boot logs.
Wrapped PT_LOAD bytes match the raw binary fingerprint used at runtime.
"""
import argparse
import hashlib
from pathlib import Path
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
p.add_argument('--elf', type=Path, required=True)
p.add_argument('--images', type=Path, required=True)
a = p.parse_args()
sys.path.insert(0, str(a.sdk.resolve()/'tools'))
from elf_parser import ELFFile
from wrap_spu_elf import wrap

elf = ELFFile(str(a.elf)); elf.load()
data = a.elf.read_bytes()
if hashlib.sha256(data).hexdigest() != '333400e0b0915e3008a22f29daf95ed2c9b03f3c2a2787cf5705222d73a5ceeb':
    raise ValueError('Expected the pinned BLUS30784 v01.00 executable')
a.images.mkdir(parents=True, exist_ok=True)
for address, size in [
    (0x0121d800, 64656), (0x0122d500, 13968), (0x01230c00, 17552),
    (0x01235100, 73616), (0x01247100, 34704), (0x0124f900, 19616),
    (0x01254600, 24720), (0x0125a700, 25232), (0x01260a00, 9488),
    (0x01262f80, 43600), (0x0126da00, 20752), (0x01272b80, 53024),
    (0x0127fb00, 16912), (0x01283d80, 67200), (0x01294400, 54544),
    (0x012a1980, 1968), (0x012a2180, 18784), (0x012a6b00, 11856),
]:
    for segment in elf.program_headers:
        if segment.p_type == 1 and segment.p_vaddr <= address and address+size <= segment.p_vaddr+segment.p_filesz:
            offset = segment.p_offset + address - segment.p_vaddr
            raw = data[offset:offset+size]
            break
    else: raise ValueError(f'Job {address:x} lies outside file-backed segments')
    out = a.images/f'zz_raw_job_{address:08X}.elf'
    out.write_bytes(wrap(raw, base=0, entry=0))
    print(f'{out.name}: {size} raw bytes')
