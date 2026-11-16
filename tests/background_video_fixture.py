"""Public, numbered SDR videos. External FFmpeg is a test oracle only."""
from pathlib import Path
import json
import struct
import subprocess
import zlib


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, **kwargs).stdout


def rgb(index, width=160, height=96):
    pixels = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            color = ((index*19) % 256, 40 if y < height//2 else 180, 32 if x < width//2 else 224)
            if y >= height-16 and x < 144:
                value = 240 if index & (1 << (x//12)) else 16
                color = (value, value, value)
            offset = (y*width+x)*3
            pixels[offset:offset+3] = bytes(color)
    return bytes(pixels)


def png(path, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    rows = b''.join(b'\0'+pixels[y*160*3:(y+1)*160*3] for y in range(96))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 160, 96, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def write(root):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    for n in range(180):
        png(root / f'source-{n:03}.png', rgb(n))
    run('ffmpeg', '-y', '-v', 'error', '-framerate', '30', '-i', root/'source-%03d.png',
        '-frames:v', '40', '-vf', 'vflip', '-c:v', 'rawvideo', '-pix_fmt', 'bgr24',
        '-flipped_raw_rgb', '1', root/'native.avi')
    for name, rate in (('native24.avi', '24'), ('native-ntsc.avi', '30000/1001')):
        run('ffmpeg', '-y', '-v', 'error', '-framerate', rate, '-i', root/'source-%03d.png',
            '-frames:v', '40', '-vf', 'vflip', '-c:v', 'rawvideo', '-pix_fmt', 'bgr24',
            '-flipped_raw_rgb', '1', root/name)
    run('ffmpeg', '-y', '-v', 'error', '-framerate', '30000/1001', '-i', root/'source-%03d.png',
        '-frames:v', '180', '-c:v', 'mpeg4', '-bf', '2', '-g', '12', '-q:v', '2', root/'ntsc.mp4')
    # Reorder/different durations expose timestamp/seek errors hidden by CFR.
    sequence = root/'vfr.txt'
    sequence.write_text(''.join(f"file 'source-{n:03}.png'\nduration {(.04,.08,.12)[n%3]}\n"
                                for n in range(12)) + "file 'source-011.png'\n", encoding='utf-8')
    run('ffmpeg', '-y', '-v', 'error', '-f', 'concat', '-safe', '0', '-i', sequence,
        '-fps_mode', 'vfr', '-c:v', 'mpeg4', '-bf', '2', '-q:v', '2', '-pix_fmt', 'yuv420p', root/'variable.mkv')
    run('ffmpeg', '-y', '-v', 'error', '-display_rotation', '90', '-i', root/'ntsc.mp4',
        '-c', 'copy', root/'rotated.mp4')
    run('ffmpeg', '-y', '-v', 'error', '-framerate', '30', '-i', root/'source-%03d.png',
        '-frames:v', '4', '-vf', 'setsar=2/1', '-c:v', 'mpeg4', '-q:v', '2', root/'wide.mp4')
    for name in ('rotated.mp4', 'wide.mp4'):
        (root/(name+'.rgba')).write_bytes(run('ffmpeg', '-v', 'error', '-i', root/name,
                                              '-vf', 'scale=w=iw*sar:h=ih:flags=bilinear+accurate_rnd,setsar=1',
                                              '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgba', '-'))
    # The video's audio must never become a second project WAV track.
    import wave
    with wave.open(str(root/'ignored.wav'), 'wb') as audio:
        audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(48000)
        audio.writeframes(struct.pack('<h', 12000)*96000)
    run('ffmpeg', '-y', '-v', 'error', '-i', root/'ntsc.mp4', '-i', root/'ignored.wav',
        '-c:v', 'copy', '-c:a', 'aac', root/'with-audio.mp4')
    reports = []
    for name in ('native.avi', 'ntsc.mp4', 'variable.mkv', 'with-audio.mp4'):
        report = json.loads(run('ffprobe', '-v', 'error', '-select_streams', 'v:0',
                               '-show_frames', '-show_streams', '-of', 'json', root/name))
        decoded = run('ffmpeg', '-v', 'error', '-i', root/name, '-map', '0:v:0',
                      '-fps_mode', 'passthrough', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-')
        assert len(decoded) == len(report['frames'])*160*96*3
        frames = []
        origin = float(report['streams'][0].get('start_time', '0'))
        for n, frame in enumerate(report['frames']):
            data = decoded[n*160*96*3:(n+1)*160*96*3]
            number = sum((1 << bit) for bit in range(12) if data[(88*160+bit*12+6)*3] > 128)
            frames.append(dict(ptsUs=round((float(frame['best_effort_timestamp_time'])-origin)*1e6), number=number))
        reports.append(dict(file=name, frames=frames))
    (root/'videos.json').write_text(json.dumps(reports, indent=2)+'\n', encoding='utf-8')
    if __import__('sys').platform == 'win32':
        vfw_reference(root)
    return reports


def vfw_reference(root):
    """Original AVIFIL32 sampling and raw sample reads; no codec/GPU/window."""
    import ctypes as c
    v = c.WinDLL('avifil32')
    v.AVIFileOpenW.argtypes = [c.POINTER(c.c_void_p), c.c_wchar_p, c.c_uint, c.c_void_p]
    v.AVIFileGetStream.argtypes = [c.c_void_p, c.POINTER(c.c_void_p), c.c_uint, c.c_int]
    v.AVIStreamSampleToTime.argtypes = v.AVIStreamTimeToSample.argtypes = [c.c_void_p, c.c_int]
    v.AVIStreamRead.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_void_p, c.c_int,
                               c.POINTER(c.c_int), c.POINTER(c.c_int)]
    for name in ('AVIFileRelease', 'AVIStreamRelease'):
        getattr(v, name).argtypes = [c.c_void_p]
    v.AVIFileInit()
    results = []
    try:
        for name in ('native.avi', 'native24.avi', 'native-ntsc.avi'):
            file, stream = c.c_void_p(), c.c_void_p()
            assert v.AVIFileOpenW(c.byref(file), str(root/name), 0x40, None) == 0
            assert v.AVIFileGetStream(file, c.byref(stream), int.from_bytes(b'vids', 'little'), 0) == 0
            frame_ms = v.AVIStreamSampleToTime(stream, 1)
            picks = []
            try:
                for native in range(45):
                    ordinal = native if frame_ms == 33 else v.AVIStreamTimeToSample(stream, native*1000//30)
                    ordinal = max(0, min(39, ordinal))
                    data = c.create_string_buffer(160*96*3)
                    byte_count, sample_count = c.c_int(), c.c_int()
                    assert v.AVIStreamRead(stream, ordinal, 1, data, len(data),
                                           c.byref(byte_count), c.byref(sample_count)) == 0
                    assert byte_count.value == len(data) and sample_count.value == 1
                    data = data.raw[(95-88)*160*3:(96-88)*160*3]
                    number = sum(1 << bit for bit in range(12) if data[(bit*12+6)*3] > 128)
                    assert number == ordinal
                    picks.append(dict(nativeFrame=native, number=number))
                results.append(dict(file=name, frameMs=frame_ms, picks=picks))
            finally:
                v.AVIStreamRelease(stream); v.AVIFileRelease(file)
    finally:
        v.AVIFileExit()
    (root/'vfw-reference.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    import sys
    write(sys.argv[1])
