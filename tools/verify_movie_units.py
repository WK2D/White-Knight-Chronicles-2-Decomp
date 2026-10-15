"""Compare C++-assembled private movie units with an independent FFmpeg decode.

Requires PyAV. Run wkc2_mpegps_test <movie> <private-prefix> first. No game bytes,
frames, local paths or machine details are written to the verification record.
"""
import argparse
import hashlib
import json
from pathlib import Path

import av


def file_hash(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def frame_hash(frame):
    frame = frame.reformat(format='yuv420p')
    h = hashlib.sha256()
    for plane in frame.planes:
        data = bytes(plane)
        for row in range(plane.height):
            start = row * plane.line_size
            h.update(data[start:start + plane.width])
    return h.digest()


def verify(movie, prefix):
    sizes = [int(line.split()[0]) for line in Path(str(prefix) + '.sizes').read_text().splitlines()]
    codec = av.CodecContext.create('mpeg2video', 'r')
    actual = []
    with Path(str(prefix) + '.m2v').open('rb') as data:
        for size in sizes:
            packet = data.read(size)
            if len(packet) != size:
                raise ValueError('truncated exported access unit')
            actual.extend(frame_hash(frame) for frame in codec.decode(av.Packet(packet)))
        if data.read(1):
            raise ValueError('exported video has bytes not covered by the unit list')
        actual.extend(frame_hash(frame) for frame in codec.decode(None))
    # Explicit MPEG format: .pam is also a still-image extension in FFmpeg.
    with av.open(str(movie), format='mpeg') as reference:
        stream = reference.streams.video[0]
        expected = [frame_hash(frame) for frame in reference.decode(video=0)]
        width, height = stream.width, stream.height
    if not actual or actual != expected:
        raise ValueError('assembled units differ from independent movie decode')
    record = {
        'input_sha256': file_hash(movie),
        'video_units': len(sizes),
        'decoded_frames': len(actual),
        'all_frame_pixels_match_independent_demux_and_decode': True,
        'width': width, 'height': height, 'comparison_format': 'yuv420p',
        'ordered_frame_hashes_sha256': hashlib.sha256(b''.join(actual)).hexdigest(),
        'video_elementary_stream_sha256': file_hash(Path(str(prefix) + '.m2v')),
        'independent_decoder': 'FFmpeg through PyAV',
        'pyav_version': av.__version__,
        'ffmpeg_libraries': {key: list(value) for key, value in av.library_versions.items()},
        'native_playback_verified': False,
    }
    ac3 = Path(str(prefix) + '.ac3')
    pcm = Path(str(prefix) + '.pcm')
    if ac3.exists():
        frames = samples = 0
        with av.open(str(ac3)) as audio:
            rate = audio.streams.audio[0].sample_rate
            for frame in audio.decode(audio=0):
                frames += 1
                samples += frame.samples
        record['audio'] = {'codec': 'AC3', 'independently_decoded_frames': frames,
                           'samples_per_channel': samples, 'sample_rate': rate,
                           'elementary_stream_sha256': file_hash(ac3)}
    elif pcm.exists():
        record['audio'] = {'codec': 'LPCM', 'bytes': pcm.stat().st_size,
                           'elementary_stream_sha256': file_hash(pcm),
                           'independent_audio_decode_verified': False}
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('movie', type=Path)
    parser.add_argument('private_prefix', type=Path)
    parser.add_argument('--record', type=Path)
    args = parser.parse_args()
    result = verify(args.movie, args.private_prefix)
    content = json.dumps(result, indent=2) + '\n'
    if args.record:
        args.record.write_text(content, encoding='utf-8')
    print(content, end='')


if __name__ == '__main__':
    main()
