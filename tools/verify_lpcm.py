"""Compare native LPCM API output with FFmpeg decoding of private stereo PCM.

First run wkc2_adec_test <private-s16be-input> <private-f32be-output>.
This checks conversion/queueing of every sample, not demuxing or host playback.
Requires PyAV; no game samples or host paths enter the verification record.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import av


def verify(source, decoded):
    raw = source.read_bytes()
    if not raw or len(raw) % 960:
        raise ValueError('input must contain complete 240-sample stereo s16be units')
    codec = av.CodecContext.create('pcm_s16be', 'r')
    codec.layout = 'stereo'
    codec.sample_rate = 48000
    expected = bytearray()
    for frame in codec.decode(av.Packet(raw)):
        if frame.format.name != 's16' or frame.layout.nb_channels != 2:
            raise ValueError('unexpected independent decoder output layout')
        data = bytes(frame.planes[0])[:frame.samples * 4]
        # FFmpeg's s16 output is native endian. Pack an explicit guest BE float.
        for (sample,) in struct.iter_unpack('=h', data):
            expected.extend(struct.pack('>f', sample / 32768))
    actual = decoded.read_bytes()
    if actual != expected:
        raise ValueError('native PCM samples differ from independent decoder')
    return {
        'codec': 'LPCM', 'access_units': len(raw) // 960,
        'samples_per_channel': len(actual) // 8, 'sample_rate': 48000,
        'channels': 2, 'output_format': 'f32be interleaved',
        'input_sha256': hashlib.sha256(raw).hexdigest(),
        'output_sha256': hashlib.sha256(actual).hexdigest(),
        'all_native_api_samples_match_ffmpeg_pcm_decode': True,
        'comparison_input': 'assembled elementary PCM; independent demux not covered',
        'native_playback_verified': False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('private_s16be_input', type=Path)
    parser.add_argument('private_f32be_output', type=Path)
    parser.add_argument('--record', type=Path)
    args = parser.parse_args()
    content = json.dumps(verify(args.private_s16be_input, args.private_f32be_output), indent=2) + '\n'
    if args.record:
        args.record.write_text(content, encoding='utf-8')
    print(content, end='')


if __name__ == '__main__':
    main()
