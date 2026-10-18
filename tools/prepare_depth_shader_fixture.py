"""Extract the SDK's actual depth-conversion shader for descriptor validation."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
match = re.search(r'static const char s_zconv_hlsl\[\] =\s*\n(.*?);\s*\n#define VK_ZCONV_MAX', source, re.S)
if not match:
    parser.error('The production depth shader declaration was not found.')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('static const char s_zconv_hlsl[] =\n' + match[1] + ';\n', encoding='utf-8')
