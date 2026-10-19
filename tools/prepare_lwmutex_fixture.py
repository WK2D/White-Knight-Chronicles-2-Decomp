"""Extract the production guest-mutex handlers for synthetic concurrency tests."""
import argparse
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
source=args.source.read_text(encoding='utf-8')
start=source.index('#define LWM_OWNER')
end=source.index('/* sys_lwcond (sysPrxForUser)',start)
region=source[start:end]
required=('std::atomic_flag g_lwm_tab_lock', 'PpuLwmutexToken token;','static LwmSlot* lwm_slot(',
          'static bool lwm_try(','static bool lwm_wait(',
          'static void lwm_release(','static DWORD lwm_timeout_ms(',
          'static void sys_lwmutex_create(','static void sys_lwmutex_lock(',
          'static void sys_lwmutex_trylock(','static void sys_lwmutex_unlock(',
          'std::memory_order_acquire','std::memory_order_release')
for text in required:
    if text not in region:raise RuntimeError('Production mutex implementation missing: '+text)
if 'lwm_sem(' in region:
    raise RuntimeError('Source still uses the previous kernel-only mutex implementation')
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(region,encoding='utf-8')
