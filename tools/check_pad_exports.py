"""Ensure source scanning keeps the complete pad API in the generated HLE table."""
from pathlib import Path
import re
import sys

table = Path(sys.argv[1]).read_text(encoding='utf-8')
required = {
    'cellPadInit', 'cellPadEnd', 'cellPadGetData', 'cellPadGetInfo',
    'cellPadGetInfo2', 'cellPadPeriphGetInfo', 'cellPadSetPortSetting',
    'cellPadInfoPressMode', 'cellPadSetPressMode', 'cellPadGetCapabilityInfo',
    'cellPadSetActDirect', 'cellPadClearBuf',
}
registered = set(re.findall(r'ps3_hle_register\(0x[0-9A-Fa-f]+u, "(cellPad\w+)",', table))
missing = required - registered
if missing:
    raise SystemExit('Pad API omitted from HLE registration: ' + ', '.join(sorted(missing)))
print('All 12 pad API exports registered.')
