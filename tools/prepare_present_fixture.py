"""Extract production Vulkan presentation functions for a recording-API test."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
functions = []
for signature in ('static void vk_window_blit(', 'static void vk_present_window(',
                  'static void vk_eng_present_window(', 'static void vk_eng_present('):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    functions.append(source[start:end])
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n\n'.join(functions) + '\n', encoding='utf-8')
