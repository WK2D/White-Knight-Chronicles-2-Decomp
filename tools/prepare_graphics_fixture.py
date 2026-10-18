"""Extract production Vulkan settings and HUD paths for recording-API checks."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();s=a.source.read_text(encoding='utf-8')
start=s.index('static vk_image s_counter_image;')
end=s.index('static int vk_eng_upload_rows(',start)
parts=[s[start:end]]
for signature in ['void rsx_vulkan_backend_configure(', 'static void vk_apply_graphics(void)\n{']:
 start=s.index(signature);end=s.index('\n}\n',start)+3;parts.append(s[start:end])
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text('\n'.join(parts),encoding='utf-8')
