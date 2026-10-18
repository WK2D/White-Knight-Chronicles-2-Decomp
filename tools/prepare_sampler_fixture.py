"""Extract the production Vulkan sampler builder for a host-only regression."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
start = source.index('static VkSampler vk_eng_sampler(')
end = source.index('/* The view a bound engine handle samples through:', start)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(source[start:end], encoding='utf-8')
