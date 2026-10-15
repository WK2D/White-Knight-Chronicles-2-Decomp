"""Analyze a privately supplied, decrypted WKC2 ELF with the pinned ps3recomp SDK.

Writes metadata and an import coverage report, never executable or asset bytes.
Runtime handler presence is a static inventory, not a correctness claim.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sdk', type=Path, required=True)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    sdk, elf_path, out = args.sdk.resolve(), args.elf.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if elf_path.read_bytes()[:4] != b'\x7fELF':
        ap.error('Expected a decrypted ELF, not a SELF container or disc image')
    subprocess.run([sys.executable, str(sdk/'tools/ppu_loader.py'), str(elf_path),
                    '-o', str(out)], check=True)
    sys.path.insert(0, str(sdk/'tools'))
    from elf_parser import ELFFile
    elf = ELFFile(str(elf_path))
    elf.load()
    code_sections = [s for s in elf.section_headers if s.sh_flags & 4 and s.sh_size]
    code_end = max(s.sh_addr + s.sh_size for s in code_sections)
    ranges = [(s.sh_addr, s.sh_addr+s.sh_size) for s in code_sections]
    original = json.loads((out/'EBOOT.functions.json').read_text())
    functions = []
    for f in original:
        start = int(f['start'], 0)
        for lo, hi in ranges:
            if lo <= start < hi:
                f['end'] = hex(min(int(f['end'], 0), hi))
                functions.append(f)
                break
    (out/'functions.bounded.json').write_text(json.dumps(functions, indent=2)+'\n')
    imports = json.loads((out/'EBOOT.imports.json').read_text())
    nid_values = sorted({x['nid'] for x in imports})
    (out/'nids.txt').write_text('\n'.join(nid_values)+'\n')
    with (out/'resolved-nids.json').open('w') as stream:
        subprocess.run([sys.executable, str(sdk/'tools/nid_database.py'),
                        '--batch', str(out/'nids.txt'), '--json'], stdout=stream, check=True)
    table = out/'hle-table.cpp'
    subprocess.run([sys.executable, str(sdk/'tools/gen_hle_nids.py'),
                    '--all', '--out', str(table)], check=True)
    table_text = table.read_text()
    # Only match registration calls; a NID in a comment is not a handler.
    handlers = {int(x, 16) for x in re.findall(
        r'ps3_hle_register\(\s*0x([0-9a-fA-F]+)[uU]?\s*,', table_text)}
    if not handlers:
        raise RuntimeError('No HLE registrations parsed; refusing a misleading coverage report')
    missing = [x for x in imports if int(x['nid'], 0) not in handlers]
    summary = {
        'title_id': 'BLUS30784', 'app_version': '01.00',
        'elf_sha256': hashlib.sha256(elf_path.read_bytes()).hexdigest(),
        'elf_size': elf_path.stat().st_size,
        'sdk_commit': subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip(),
        'entry_opd': hex(elf.elf_header.e_entry),
        'code_end': hex(code_end),
        'executable_sections': [{'start': hex(lo), 'end': hex(hi)} for lo, hi in ranges],
        'loader_function_candidates': len(original),
        'bounded_function_candidates': len(functions),
        'imports': len(imports),
        'libraries': dict(Counter(x['library'] for x in imports)),
        'imports_without_generated_handler': len(missing),
        'coverage_limit': 'Inventory of the generated global table only. Additional handlers register in the boot scaffold and this port. A table entry may be a stub or incomplete; presence does not verify behaviour.',
        'native_boot_verified': False,
    }
    (out/'missing-handlers.json').write_text(json.dumps(missing, indent=2)+'\n')
    (out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
