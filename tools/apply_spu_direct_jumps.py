"""Spell lifted SPU cross-function branches as SPU_JUMP (SDK patch 0130).

The lifter lowers a branch to another lifted function as
`{ ctx->pc = 0xX; g_spu_trampoline_fn = F; return; }`. SPU_JUMP has the same
effect, but tail-calls F directly when the drain loop's per-transfer hooks have
nothing to do. Run after the native hook installers: their marked regions keep
the exact lifted text their pinned hashes describe, so they are left alone.
Idempotent; game code is never written to the public tree.
"""
import argparse
import re
from pathlib import Path

JUMP = re.compile(r'\{ ctx->pc = (0x[0-9A-F]+); g_spu_trampoline_fn = ([A-Za-z_][A-Za-z0-9_]*); return; \}')
REGION = re.compile(r'/\* (WKC_[A-Z_]+)_BEGIN \*/\n.*?/\* \1_END \*/\n', re.S)


def apply(text):
    out = []
    at = 0
    for region in REGION.finditer(text):
        out.append(JUMP.sub(r'SPU_JUMP(ctx, \1, \2);', text[at:region.start()]))
        out.append(region.group(0))
        at = region.end()
    out.append(JUMP.sub(r'SPU_JUMP(ctx, \1, \2);', text[at:]))
    return ''.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('generated_spu', type=Path)
    args = parser.parse_args()
    files = changed = jumps = 0
    for path in sorted(args.generated_spu.glob('*/spu_recomp.c')):
        before = path.read_text(encoding='utf-8')
        after = apply(before)
        files += 1
        jumps += after.count('SPU_JUMP(ctx, ')
        if after != before:
            path.write_text(after, encoding='utf-8', newline='\n')
            changed += 1
    if not files:
        raise SystemExit('No lifted SPU sources found')
    print(f'SPU direct jumps: {jumps} branch sites in {files} images ({changed} updated)')


if __name__ == '__main__':
    main()
