"""Generate a private AC-3 sample reference and optionally verify the native API.

Requires PyAV and matching FFmpeg libraries. Input is an assembled elementary
AC-3 stream: this comparison does not independently validate container demuxing.
No game bytes or host paths are written into the JSON verification record.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import av


def reference(source, output, channels):
    decoder = av.CodecContext.create('ac3', 'r')
    decoder.options = {'cons_noisegen': '1', 'drc_scale': '1',
                       'heavy_compr': '0', 'target_level': '0'}
    if channels == 2:
        decoder.options['downmix'] = 'stereo'
    frames = samples = 0
    digest = hashlib.sha256()
    order = [0, 1] if channels == 2 else [0, 2, 1, 4, 5, 3]
    with av.open(str(source), format='ac3') as container, output.open('wb') as target:
        for packet in container.demux(audio=0):
            for frame in decoder.decode(packet):
                if (frame.format.name != 'fltp' or frame.layout.nb_channels != channels
                        or frame.sample_rate != 48000 or frame.samples != 1536):
                    raise ValueError('unsupported reference format/rate/frame size')
                planes = [struct.unpack('=' + 'f' * frame.samples,
                                        bytes(p)[:frame.samples * 4]) for p in frame.planes]
                data = b''.join(struct.pack('>' + 'f' * channels,
                                            *(planes[ch][n] for ch in order))
                                for n in range(frame.samples))
                target.write(data)
                digest.update(data)
                frames += 1
                samples += frame.samples
    if not frames:
        raise ValueError('no decoded audio frames')
    return frames, samples, digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('private_ac3_input', type=Path)
    parser.add_argument('private_reference_output', type=Path)
    parser.add_argument('--channels', type=int, choices=[2, 6], default=2)
    parser.add_argument('--decoder-test', type=Path)
    parser.add_argument('--record', type=Path)
    args = parser.parse_args()
    frames, samples, digest = reference(args.private_ac3_input,
                                       args.private_reference_output, args.channels)
    verified = False
    if args.decoder_test:
        log = args.private_reference_output.with_suffix('.native.log')
        with log.open('wb') as stream:
            result = subprocess.run([str(args.decoder_test.resolve()), 'ac3',
                                     str(args.private_ac3_input.resolve()),
                                     str(args.private_reference_output.resolve()),
                                     str(args.channels)], stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode == 77:
            raise RuntimeError('native decoder skipped: matching FFmpeg runtime unavailable')
        if result.returncode:
            raise RuntimeError('native API comparison failed; inspect the private log')
        verified = True
    record = {
        'codec': 'AC-3', 'input': 'assembled elementary stream',
        'input_sha256': hashlib.sha256(args.private_ac3_input.read_bytes()).hexdigest(),
        'frames': frames, 'samples_per_channel': samples, 'sample_rate': 48000,
        'output_channels': args.channels, 'output_format': 'interleaved f32be',
        'channel_order': 'L,R' if args.channels == 2 else 'L,C,R,SL,SR,LFE',
        'reference_sha256': digest, 'independent_decoder': 'FFmpeg through PyAV',
        'all_native_api_samples_match_reference': verified,
        'native_sequence_passes': 2 if verified else 0,
        'input_copy_and_output_backpressure_tested': verified and frames > 16,
        'independent_container_audio_demux_verified': False,
        'in_game_playback_verified': False,
    }
    content = json.dumps(record, indent=2) + '\n'
    if args.record:
        args.record.write_text(content, encoding='utf-8')
    print(content, end='')


if __name__ == '__main__':
    main()
