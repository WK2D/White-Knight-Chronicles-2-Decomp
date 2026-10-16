"""Prepare pinned public FFmpeg headers in the private dependency directory."""
import argparse
import hashlib
from pathlib import Path
import sys
import tarfile
import urllib.request

VERSION = '8.1'
SHA256 = 'b072aed6871998cce9b36e7774033105ca29e33632be5b6347f3206898e0756a'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dependency_directory', type=Path)
    args = parser.parse_args()
    root = args.dependency_directory
    root.mkdir(parents=True, exist_ok=True)
    archive = root / f'ffmpeg-{VERSION}.tar.xz'
    if not archive.exists():
        urllib.request.urlretrieve(f'https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz', archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise ValueError('FFmpeg archive does not match the pinned SHA-256')
    source = root / f'ffmpeg-{VERSION}'
    marker = source / '.wkc2-headers'
    if not marker.exists():
        with tarfile.open(archive) as package:
            members = [member for member in package.getmembers()
                       if member.isfile() and member.name.endswith('.h')
                       and member.name.startswith((f'ffmpeg-{VERSION}/libavcodec/',
                                                   f'ffmpeg-{VERSION}/libavutil/'))]
            package.extractall(root, members=members, filter='data')
        marker.write_text(SHA256 + '\n', encoding='ascii')
    # These are the two public configure results; conservative unaligned access
    # works on both target host architectures. No FFmpeg implementation is built.
    config = ('#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n'
              f'#define AV_HAVE_BIGENDIAN {int(sys.byteorder == "big")}\n'
              '#define AV_HAVE_FAST_UNALIGNED 0\n#endif\n')
    (source / 'libavutil/avconfig.h').write_text(config, encoding='ascii')
    print(f'FFmpeg {VERSION} headers verified')


if __name__ == '__main__':
    main()
