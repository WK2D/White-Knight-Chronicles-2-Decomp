"""Extract the production Vulkan arena reservation for a host-only regression."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
start = source.index('static VkDeviceSize vk_eb_align(')
end = source.index('static void vk_eb_shutdown(', start)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(source[start:end], encoding='utf-8')
