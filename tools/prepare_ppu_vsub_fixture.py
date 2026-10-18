"""Decode and lift original synthetic VMX instructions for the regression."""
from pathlib import Path
import sys

sdk, output = map(Path, sys.argv[1:3])
sys.path.insert(0, str(sdk / 'tools'))
from ppu_lifter import PPULifter, LiftedFunction, SOURCE_PREAMBLE
from ppu_disasm import decode

output.parent.mkdir(parents=True, exist_ok=True)
lifter = PPULifter()
parts = [lifter.emit_header(), SOURCE_PREAMBLE.replace('#include "ppu_recomp.h"', '')]
for name, vd, va, vb in [('separate', 1, 2, 3), ('alias_a', 2, 2, 3),
                         ('alias_b', 3, 2, 3), ('all_same', 2, 2, 2)]:
    ins = decode((4 << 26) | (vd << 21) | (va << 16) | (vb << 11) | 1664)
    assert ins.mnemonic == 'vsubuws'
    parts.append('static void ' + name + '(ppu_context* ctx) {\n' +
                 lifter._translate(ins, LiftedFunction()) + '\n}\n')
output.write_text('\n'.join(parts), encoding='utf-8', newline='\n')
