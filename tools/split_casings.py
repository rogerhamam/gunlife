"""Split a pack of casing-impact recordings into one WAV per hit.

The download is a single MP3 with every impact in it separated by silence.
This finds the individual hits, trims them and writes them out as
assets/sounds/casing_N.wav, which the game loads as one sound bank and picks
from at random each time a spent case lands.

Detection is on a short-window RMS envelope rather than raw samples, so the
decay tail of a hit does not get chopped off the moment a single sample dips
below the threshold.

  python tools/split_casings.py [source.mp3]
"""
import os
import sys
import wave

import miniaudio
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOUNDS = os.path.join(ROOT, "assets", "sounds")
DEFAULT_SRC = os.path.join(
    os.path.expanduser("~"), "Downloads",
    "Bullet Shell Casing Impact Sound Effects Pack.mp3")

# A hit has to clear this fraction of the loudest point in the file to count,
# and has to be at least this long, so tape hiss and clicks are ignored.
OPEN_FRAC = 0.055
CLOSE_FRAC = 0.012
MIN_MS = 45
MAX_MS = 1400
GAP_MS = 70          # silence this long ends a hit
PAD_MS = 12          # kept either side so nothing is clipped


def decode(path):
    d = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.SIGNED16)
    a = np.asarray(d.samples, dtype=np.float32) / 32768.0
    if d.nchannels > 1:
        a = a.reshape(-1, d.nchannels).mean(axis=1)
    return a, d.sample_rate


def envelope(a, sr, window_ms=8):
    n = max(1, int(sr * window_ms / 1000.0))
    # RMS over a sliding window, via a cumulative sum of squares.
    sq = np.concatenate(([0.0], np.cumsum(a.astype(np.float64) ** 2)))
    out = np.sqrt(np.maximum(0.0, (sq[n:] - sq[:-n]) / n))
    return np.concatenate((out, np.full(len(a) - len(out), out[-1] if len(out) else 0.0)))


def find_hits(env, sr):
    peak = float(env.max())
    if peak <= 0:
        return []
    hi, lo = peak * OPEN_FRAC, peak * CLOSE_FRAC
    gap = int(sr * GAP_MS / 1000.0)
    hits, start, quiet = [], None, 0
    for i, v in enumerate(env):
        if start is None:
            if v >= hi:
                start = i
                quiet = 0
        else:
            if v < lo:
                quiet += 1
                if quiet >= gap:
                    hits.append((start, i - quiet))
                    start = None
            else:
                quiet = 0
            if start is not None and (i - start) > sr * MAX_MS / 1000.0:
                hits.append((start, i))
                start = None
    if start is not None:
        hits.append((start, len(env) - 1))
    return [(a, b) for a, b in hits if (b - a) >= sr * MIN_MS / 1000.0]


def write_wav(path, a, sr):
    a = np.clip(a, -1.0, 1.0)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((a * 32767.0).astype("<i2").tobytes())


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    if not os.path.exists(src):
        sys.exit(f"missing {src}")
    a, sr = decode(src)
    env = envelope(a, sr)
    hits = find_hits(env, sr)
    if not hits:
        sys.exit("no impacts found -- try lowering OPEN_FRAC")

    # Clear out a previous run so a shorter pack does not leave orphans behind.
    for f in os.listdir(SOUNDS):
        if f.startswith("casing_") and f.endswith(".wav"):
            os.remove(os.path.join(SOUNDS, f))

    pad = int(sr * PAD_MS / 1000.0)
    written = 0
    for start, end in hits:
        s = max(0, start - pad)
        e = min(len(a), end + pad)
        clip = a[s:e].copy()
        # Normalise each hit to the same level: the pack has them at wildly
        # different distances and they need to sit at one volume in game.
        m = float(np.max(np.abs(clip)))
        if m > 1e-4:
            clip *= 0.82 / m
        # Short fades so the cuts are inaudible.
        f = min(len(clip) // 4, int(sr * 0.004))
        if f > 1:
            clip[:f] *= np.linspace(0.0, 1.0, f)
            clip[-f:] *= np.linspace(1.0, 0.0, f)
        out = os.path.join(SOUNDS, f"casing_{written}.wav")
        write_wav(out, clip, sr)
        written += 1

    print(f"{written} impacts written to {SOUNDS} at {sr} Hz "
          f"(source {len(a) / sr:.1f}s)")


if __name__ == "__main__":
    main()
