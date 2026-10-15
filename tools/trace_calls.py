"""Add opt-in return-value diagnostics inside selected lifted PPU functions.

Only changes generated sources. Run again after a fresh lift. Enabled at run
time with WKC2_TRACE_CALLS=1; leaves original register values untouched.
"""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('generated', type=Path)
p.add_argument('functions', nargs='+', type=lambda s: int(s, 16))
a = p.parse_args()
for addr in a.functions:
    found = False
    for path in a.generated.glob('ppu_recomp_*.cpp'):
        text = path.read_text()
        start = text.find(f'void func_{addr:08X}(ppu_context* ctx) {{')
        if start < 0: continue
        end = text.find('\nvoid func_', start + 1)
        if end < 0: end = len(text)
        body = text[start:end]
        marker = f'[WKC2-CALL:{addr:08X}]'
        body = re.sub(r'\s*if \(getenv\("WKC2_TRACE_CALLS"\)\) fprintf\(stderr, "' +
            re.escape(marker) + r'[^\n]*?\);', '', body)
        body = re.sub(r'(func_[0-9A-F]{8}\(ctx\);)(?!\s*DRAIN_TRAMPOLINE\(ctx\);)',
            r'\1 DRAIN_TRAMPOLINE(ctx);', body)
        # A tail branch queues a trampoline; the result exists only after drain.
        body = re.sub(r'(func_([0-9A-F]{8})\(ctx\);\s*DRAIN_TRAMPOLINE\(ctx\);)',
            lambda m: f'\n        if (getenv("WKC2_TRACE_CALLS")) fprintf(stderr, "{marker} enter={m[2]} a3=%llX a4=%llX a5=%llX a6=%llX\\n", (unsigned long long)ctx->gpr[3], (unsigned long long)ctx->gpr[4], (unsigned long long)ctx->gpr[5], (unsigned long long)ctx->gpr[6]);\n        ' + m[1] + f'\n        if (getenv("WKC2_TRACE_CALLS")) fprintf(stderr, "{marker} callee={m[2]} lr=%08X r3=%08X\\n", (uint32_t)ctx->lr, (uint32_t)ctx->gpr[3]);', body)
        text = text[:start] + body + text[end:]
        if '#include <stdlib.h>' not in text:
            text = '#include <stdlib.h>\n' + text
        path.write_text(text)
        print(f'Traced {addr:08X} in {path.name}')
        found = True
        break
    if not found: raise ValueError(f'Function {addr:08X} not found')
