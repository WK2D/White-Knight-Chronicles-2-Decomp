"""Extract production batch-retirement helpers for recording-call regression tests."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
parts = []
for name in ('wt_vk_wait', 'vk_batch_retire', 'vk_slot_drain', 'vk_defer_destroy_image'):
    match = re.search(r'static (?:VkResult|void|int) ' + name + r'\([^;]*?\)\s*\{', source)
    if not match:
        raise RuntimeError('Production function missing: ' + name)
    start = match.start()
    position = match.end()
    depth = 1
    while depth:
        char = source[position]
        depth += (char == '{') - (char == '}')
        position += 1
    parts.append(source[start:position])
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n\n'.join(parts) + '\n', encoding='utf-8')
