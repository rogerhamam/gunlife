"""Build the super shotgun's audio.

Neither Grand Theft Jack 3D (60 clips) nor Naval Command (29 clips) ships a
double-barrel report, and there is nothing usable in the download folder
either -- so rather than reuse the pump shotgun at a lower pitch, this
synthesises a proper super shotgun from GTJ3D's own snd_shotgun.wav. It stays
in the family (same source recording, same room) while reading as a much
bigger, twin-bore gun.

The report is built in layers:

  barrel A   the source pitched down ~2.6 semitones
  barrel B   pitched down further and delayed 16 ms, so the two bores fire
             just far enough apart to smear the transient
  sub        a 90 -> 45 Hz sine sweep for the chest punch a 12-gauge has and
             an 8-bit sample does not
  wash       band-limited noise under an exponential decay: muzzle blast
  slap       a low-passed, delayed copy of the mix, for the report bouncing
             back off the street

Also emits a break-open clack for the reload, made from snd_reload_shotgun.

  python tools/make_ssg_sound.py
"""
import os
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOUNDS = os.path.join(ROOT, "assets", "sounds")


def read_wav(path):
    """Return (mono float32 in -1..1, sample rate)."""
    with wave.open(path, "rb") as w:
        ch, sw, sr, n = (w.getnchannels(), w.getsampwidth(),
                         w.getframerate(), w.getnframes())
        raw = w.readframes(n)
    if sw == 1:                       # 8-bit PCM is unsigned
        a = np.frombuffer(raw, dtype=np.uint8).astype(np.float32)
        a = (a - 128.0) / 128.0
    elif sw == 2:
        a = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    else:
        raise SystemExit(f"{path}: unsupported sample width {sw}")
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return a, sr


def write_wav(path, a, sr):
    a = np.clip(a, -1.0, 1.0)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((a * 32767.0).astype("<i2").tobytes())


def resample(a, factor):
    """Play `a` at `factor` speed -- <1 is slower and lower."""
    n = int(len(a) / factor)
    src = np.arange(n, dtype=np.float32) * factor
    return np.interp(src, np.arange(len(a), dtype=np.float32), a).astype(np.float32)


def lowpass(a, sr, cutoff):
    """One-pole low pass. Crude, but these are impulses -- phase does not matter."""
    k = float(np.exp(-2.0 * np.pi * cutoff / sr))
    out = np.empty_like(a)
    acc = 0.0
    for i, v in enumerate(a):     # short clips; a Python loop is fine here
        acc = acc * k + v * (1.0 - k)
        out[i] = acc
    return out


def mix_into(dst, src, offset, gain):
    end = min(len(dst), offset + len(src))
    if end > offset:
        dst[offset:end] += src[:end - offset] * gain


def normalise(a, peak=0.94):
    m = float(np.max(np.abs(a)))
    return a * (peak / m) if m > 1e-6 else a


def envelope(a, sr, attack_ms, release_ms):
    n = len(a)
    at = max(1, int(sr * attack_ms / 1000.0))
    rl = max(1, int(sr * release_ms / 1000.0))
    a[:at] *= np.linspace(0.0, 1.0, at, dtype=np.float32)
    a[n - rl:] *= np.linspace(1.0, 0.0, rl, dtype=np.float32)
    return a


def build_report(src, sr):
    a_bar = resample(src, 0.86)      # ~2.6 semitones down
    b_bar = resample(src, 0.82)      # second bore, a touch lower still
    total = int(sr * 0.85)
    out = np.zeros(total, dtype=np.float32)

    mix_into(out, a_bar, 0, 1.00)
    mix_into(out, b_bar, int(sr * 0.016), 0.90)

    # Sub: 90 -> 45 Hz sweep, fast exponential decay. This is what makes it
    # read as a big bore rather than a loud small one.
    t = np.arange(int(sr * 0.30), dtype=np.float32) / sr
    freq = 90.0 * np.exp(-t * 7.0) + 45.0
    phase = 2.0 * np.pi * np.cumsum(freq) / sr
    sub = np.sin(phase).astype(np.float32) * np.exp(-t * 11.0)
    mix_into(out, sub, int(sr * 0.004), 0.62)

    # Muzzle blast wash: band-limited noise, quick decay.
    rng = np.random.default_rng(7)   # fixed seed: rebuilds are identical
    t = np.arange(int(sr * 0.22), dtype=np.float32) / sr
    wash = rng.standard_normal(len(t)).astype(np.float32) * np.exp(-t * 26.0)
    wash = lowpass(wash, sr, 2600.0)
    mix_into(out, wash, 0, 0.55)

    # Street slap: the report coming back off the buildings.
    slap = lowpass(out.copy(), sr, 1400.0)
    mix_into(out, slap, int(sr * 0.034), 0.28)

    return envelope(normalise(out), sr, 1.0, 60.0)


def build_break(src, sr):
    """Break-open clack: the same mechanism, bigger and slower."""
    a = resample(src, 0.78)
    out = np.zeros(int(len(a) * 1.15), dtype=np.float32)
    mix_into(out, a, 0, 1.0)
    mix_into(out, resample(src, 0.71), int(sr * 0.010), 0.45)
    return envelope(normalise(out, 0.88), sr, 1.0, 25.0)


def main():
    fire_src = os.path.join(SOUNDS, "snd_shotgun.wav")
    load_src = os.path.join(SOUNDS, "snd_reload_shotgun.wav")
    for p in (fire_src, load_src):
        if not os.path.exists(p):
            sys.exit(f"missing {p} -- run tools/stage_assets.py first")

    a, sr = read_wav(fire_src)
    out = os.path.join(SOUNDS, "snd_supershotgun.wav")
    write_wav(out, build_report(a, sr), sr)
    print(f"wrote {out}  ({sr} Hz)")

    a, sr = read_wav(load_src)
    out = os.path.join(SOUNDS, "snd_ssg_break.wav")
    write_wav(out, build_break(a, sr), sr)
    print(f"wrote {out}  ({sr} Hz)")


if __name__ == "__main__":
    main()
