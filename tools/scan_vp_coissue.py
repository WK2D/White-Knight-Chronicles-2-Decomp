"""Count co-issue hazards in captured RSX vertex programs. Prints no program bytes.

An NV40 vertex-program instruction runs its vector and scalar operations
together; both read operands before either writes. This reports instructions
where emitting them in sequence could differ: the vector unit writes the temp
component, or (by ARL) the address register component, that the scalar
operand reads, or updates the condition register that predicates the scalar
write. Patch 0043 covers the first two; the third is only counted.

Input: vs_*.bin from RSX_CAP_GEOMETRY (D3D12) or RSX_ENGINE_SHADER_DUMP
(the shared renderer, including Vulkan).
"""
import argparse
import json
from pathlib import Path
import struct

FLOW = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x11, 0x12, 0x13, 0x14}
ARL = 0x0D
# Scalar operand components read, as positions in the source swizzle.
SCALAR_READS = {0x01: (0, 1, 2, 3), 0x07: (0, 1, 3)}


def instructions(code):
    if not code or len(code) % 16:
        raise ValueError('vertex-program capture must contain complete 16-byte instructions')
    for offset in range(0, len(code) - 15, 16):
        words = struct.unpack_from('<4I', code, offset)
        yield words
        if words[3] & 1:
            return
    raise ValueError('vertex-program capture has no end instruction')


def hazards(d0, d1, d2, d3):
    vec_op, sca_op = (d1 >> 22) & 0x1F, (d1 >> 27) & 0x1F
    vec_mask = [(d3 >> bit) & 1 for bit in (16, 15, 14, 13)]   # x, y, z, w
    sca_mask = (d3 >> 17) & 0xF
    if not (any(vec_mask) and vec_op and sca_mask and sca_op and sca_op not in FLOW):
        return None
    src2 = ((d3 >> 21) & 0x7FF) | ((d2 & 0x3F) << 11)
    swizzle = [(src2 >> shift) & 3 for shift in (14, 12, 10, 8)]
    read = {swizzle[i] for i in SCALAR_READS.get(sca_op, (0,))}
    found = {'coissued': 1, 'temp': 0, 'address': 0, 'condition': 0}
    dst_tmp = (d0 >> 15) & 0x3F
    if (src2 & 3) == 1 and vec_op != ARL and dst_tmp != 0x3F and \
            ((src2 >> 2) & 31) == (dst_tmp & 31) and any(vec_mask[c] for c in read):
        found['temp'] = 1
    if vec_op == ARL and (src2 & 3) == 3 and (d3 >> 1) & 1 and vec_mask[d0 & 3]:
        found['address'] = 1
    cc_update = (d0 >> 14) & 1 and (d0 >> 29) & 1
    if cc_update and (d0 >> 13) & 1 and ((d0 >> 10) & 7) != 7:
        found['condition'] = 1
    return found


def scan(paths):
    totals = {'programs': 0, 'instructions': 0, 'coissued': 0, 'temp': 0, 'address': 0, 'condition': 0}
    affected = {}
    for index, root in enumerate(paths):
        root = Path(root)
        if not root.is_dir():
            raise ValueError('capture directory does not exist')
        for path in sorted(root.rglob('vs_*.bin')):
            totals['programs'] += 1
            counts = {'temp': 0, 'address': 0, 'condition': 0}
            for words in instructions(path.read_bytes()):
                totals['instructions'] += 1
                found = hazards(*words)
                if not found:
                    continue
                for key, value in found.items():
                    totals[key] += value
                    if key in counts:
                        counts[key] += value
            if any(counts.values()):
                # Relative names preserve repeated vs_000.bin entries from
                # different frame directories without exposing host paths.
                name = path.relative_to(root).as_posix()
                affected[name if len(paths) == 1 else f'{index}/{name}'] = counts
    if not totals['programs']:
        raise ValueError('no vs_*.bin captures found; zero hazards cannot be inferred')
    return {'totals': totals, 'affected_programs': affected}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', nargs='+', type=Path)
    args = parser.parse_args()
    try:
        result = scan(args.captures)
    except (OSError, ValueError) as error:
        parser.exit(2, f'Capture scan failed: {error.__class__.__name__}; check the private input directories and instruction framing.\n')
    print(json.dumps(result, indent=2))
