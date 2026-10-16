"""Lift original synthetic kernel-query instructions for the ABI regression."""
from pathlib import Path
import struct
import sys

sdk, output = map(Path, sys.argv[1:3])
sys.path.insert(0, str(sdk / 'tools'))
from spu_disasm import disassemble_spu
from spu_lifter import SPULifter

output.mkdir(parents=True, exist_ok=True)
lifter = SPULifter(prefix='abi_fixture_')
# Canonical standard query, plus a changed lane-selection negative control.
for address, rotate in [(0, 0x3F820183), (0x40, 0x3F810183)]:
    instructions = list(disassemble_spu(struct.pack('>3I', 0x30803803, rotate, 0x35000000), address))
    lifter.lift_function(instructions, address, address + 12)
header = lifter.emit_header().replace('../../runtime/spu/spu_context.h', 'spu_context.h')
source = lifter.emit_source('fixture.h').replace('../../runtime/spu/spu_helpers.h', 'spu_helpers.h')
(output / 'fixture.h').write_text(header, encoding='utf-8', newline='\n')
(output / 'fixture.c').write_text(source, encoding='utf-8', newline='\n')
