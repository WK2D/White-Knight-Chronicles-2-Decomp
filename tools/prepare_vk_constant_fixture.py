"""Extract production constant binding, allocation and reset helpers for tests."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')

def function(name):
    match = re.search(r'static (?:void|int|VkDeviceSize) ' + name + r'\([^;]*?\)\s*\{', source)
    if not match:
        raise RuntimeError('Production function missing: ' + name)
    position = match.end()
    depth = 1
    while depth:
        char = source[position]
        depth += (char == '{') - (char == '}')
        position += 1
    return source[match.start():position]

# Taking an offset before reserve would retain an offset across a possible
# slot switch. Teardown also must discard identities before handles can recur.
draw = source[source.index('/* Everything that can flush the batch'):]
assert draw.index('if (vk_eb_reserve(') < draw.index('const VkDeviceSize vsoff = vk_eng_constant_take(')
assert 'rsx_constant_forget(&s_constant_reuse)' in function('vk_eb_shutdown')
names = ('vk_constant_reuse_enabled', 'vk_eb_align', 'vk_eb_take',
         'vk_eng_batch_reset', 'vk_eng_bind_vs_constants',
         'vk_eng_bind_ps_constants', 'vk_eng_constant_take')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n\n'.join(function(name) for name in names) + '\n', encoding='utf-8')
