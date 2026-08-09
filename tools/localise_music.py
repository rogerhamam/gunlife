"""Bring the soundtrack inside the project.

The four GTJ3D themes ship as 44.1 kHz stereo WAV and come to about 130 MB,
which is why the game used to stream them straight out of the original
download folder rather than copying them in. That worked on the machine they
were downloaded to and nowhere else: a clone had no music at all, and the path
was the one thing in the whole game that reached outside its own directory.

This resamples them to 22.05 kHz mono -- a quarter of the bytes, and for a
background track behind gunfire the difference is not audible -- and writes
them into assets/music, where the game looks first.

Pure Python on purpose: there is no ffmpeg here, and `audioop` was removed in
3.13, so the mixing and decimation are done by hand on an array of samples.

  python tools/localise_music.py [--from <dir with bgm_theme*.wav>] [--rate N]
"""
import array
import os
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "music")
DEFAULT_SRC = os.path.join(ROOT, "drive-download-20260808T053533Z-1-001",
                           "audio")
TRACKS = ["bgm_theme1", "bgm_theme2", "bgm_theme3", "bgm_theme4"]
TARGET_RATE = 22050


def convert(src, dst, target_rate):
    with wave.open(src, "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        frames = w.getnframes()
        if width != 2:
            raise ValueError(f"{os.path.basename(src)}: expected 16-bit, "
                             f"got {width * 8}-bit")
        raw = w.readframes(frames)

    samples = array.array("h")
    samples.frombytes(raw)
    if sys.byteorder == "big":
        samples.byteswap()

    # Stereo -> mono. Averaging rather than dropping a channel, so nothing
    # panned hard to one side disappears.
    if channels == 2:
        left = samples[0::2]
        right = samples[1::2]
        mono = array.array("h", [(left[i] + right[i]) // 2
                                 for i in range(len(left))])
    else:
        mono = samples

    # Decimate to the target rate. Nearest-sample, which is crude, but these
    # are already band-limited music and the artefacts sit far above anything
    # you can hear over a firefight.
    step = max(1, round(rate / target_rate))
    out_rate = rate // step
    thinned = mono[::step] if step > 1 else mono

    with wave.open(dst, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(out_rate)
        w.writeframes(thinned.tobytes())
    return rate, out_rate, os.path.getsize(src), os.path.getsize(dst)


def main():
    src_dir = DEFAULT_SRC
    if "--from" in sys.argv:
        src_dir = sys.argv[sys.argv.index("--from") + 1]
    rate = TARGET_RATE
    if "--rate" in sys.argv:
        rate = int(sys.argv[sys.argv.index("--rate") + 1])

    if not os.path.isdir(src_dir):
        sys.exit(f"no music at {src_dir}\n"
                 f"pass --from <dir> pointing at the bgm_theme*.wav files")
    os.makedirs(OUT, exist_ok=True)

    before = after = 0
    done = 0
    for name in TRACKS:
        src = os.path.join(src_dir, name + ".wav")
        if not os.path.exists(src):
            print(f"  {name}.wav missing, skipped")
            continue
        dst = os.path.join(OUT, name + ".wav")
        r0, r1, s0, s1 = convert(src, dst, rate)
        before += s0
        after += s1
        done += 1
        print(f"  {name}.wav  {r0} Hz stereo -> {r1} Hz mono   "
              f"{s0 // 1048576} MB -> {s1 // 1048576} MB")

    if done:
        print(f"{done} track(s) into assets/music: "
              f"{before // 1048576} MB -> {after // 1048576} MB")


if __name__ == "__main__":
    main()
