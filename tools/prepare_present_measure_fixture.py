"""Extract the actual SDK presentation recorder for a GPU-free regression."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding='utf-8')
start = source.index('#define LD_PRESENT_RING_CAP ')
end = source.index('static u64 g_ld_texture_cache_full', start)
output = Path(sys.argv[2])
output.write_text(source[start:end], encoding='utf-8', newline='\n')
