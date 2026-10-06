"""Synthesises the match sounds (referee whistles, crowd ambience, cheer, boo, heartbeat) as 16-bit WAV files.

    python Tools/Audio/synth_audio.py        (needs numpy)

Output: RefereeCareer/SourceArt/Audio/*.wav, imported by Tools/Unreal/setup_project.py. These are placeholders
with the right timing and character; swap in recorded stadium audio for release.
"""
import os
import wave

import numpy as np

SR = 44100
OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "RefereeCareer", "SourceArt", "Audio"))
rng = np.random.default_rng(7)


def t_axis(seconds):
    return np.arange(int(SR * seconds)) / SR


def envelope(n, attack, release):
    env = np.ones(n)
    a, r = int(SR * attack), int(SR * release)
    if a:
        env[:a] = np.linspace(0, 1, a)
    if r:
        env[-r:] *= np.linspace(1, 0, r)
    return env


def one_pole_lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    y = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def bandpass(x, lo, hi):
    return one_pole_lowpass(x, hi) - one_pole_lowpass(x, lo)


def whistle(seconds):
    """Pea whistle: two close partials warbled ~28 Hz by the pea, plus breath noise."""
    t = t_axis(seconds)
    trill = 1 + 0.035 * np.sin(2 * np.pi * 28 * t)
    am = 0.75 + 0.25 * np.sin(2 * np.pi * 28 * t + 0.6)
    phase1 = 2 * np.pi * np.cumsum(2950 * trill) / SR
    phase2 = 2 * np.pi * np.cumsum(3320 * trill) / SR
    tone = 0.6 * np.sin(phase1) + 0.35 * np.sin(phase2) + 0.08 * np.sin(2 * phase1)
    breath = bandpass(rng.standard_normal(len(t)), 1500, 6000) * 0.25
    return (tone * am + breath) * envelope(len(t), 0.012, 0.07)


def silence(seconds):
    return np.zeros(int(SR * seconds))


def crowd(seconds, brightness=1.0, excitement=0.4):
    """Thousands of voices: band-limited noise with slow swells and a faint chant pulse."""
    t = t_axis(seconds)
    noise = rng.standard_normal(len(t))
    body = bandpass(noise, 180, 1400 * brightness)
    hiss = bandpass(rng.standard_normal(len(t)), 1500, 4500) * 0.25 * brightness
    swell = 0.75 + 0.25 * np.sin(2 * np.pi * 0.11 * t) * np.sin(2 * np.pi * 0.047 * t + 1.0)
    chant = 1 + excitement * 0.18 * np.maximum(0, np.sin(2 * np.pi * 1.6 * t)) ** 3
    return (body + hiss) * swell * chant


def make_loop(x, fade=1.0):
    """Crossfade the tail into the head so the file loops without a click."""
    n = int(SR * fade)
    head, tail = x[:n], x[-n:]
    ramp = np.linspace(0, 1, n)
    out = x[n:].copy()
    out[-n:] = tail * (1 - ramp) + head * ramp
    return out


def boo(seconds):
    t = t_axis(seconds)
    voices = np.zeros(len(t))
    for _ in range(60):
        f = rng.uniform(95, 210)
        vib = 1 + 0.01 * np.sin(2 * np.pi * rng.uniform(4, 6) * t + rng.uniform(0, 6))
        ph = 2 * np.pi * np.cumsum(f * vib) / SR + rng.uniform(0, 6)
        voices += np.sin(ph) + 0.4 * np.sin(2 * ph) + 0.2 * np.sin(3 * ph)
    voices = one_pole_lowpass(voices / 60, 700)
    return (voices * 1.6 + crowd(seconds, 0.6) * 0.5) * envelope(len(t), 0.5, 1.0)


def cheer(seconds):
    t = t_axis(seconds)
    swell = np.minimum(1, t / 0.35) * np.exp(-np.maximum(0, t - 0.8) / 1.6)
    return crowd(seconds, 1.8, 1.0) * (0.4 + 1.6 * swell)


def heartbeat():
    t = t_axis(0.9)
    beat = np.zeros(len(t))
    for start, amp in ((0.0, 1.0), (0.22, 0.7)):
        tt = t - start
        mask = tt >= 0
        beat[mask] += amp * np.sin(2 * np.pi * 52 * tt[mask]) * np.exp(-tt[mask] / 0.07)
    return beat


def write(name, x, peak=0.85):
    os.makedirs(OUT, exist_ok=True)
    x = x / (np.max(np.abs(x)) + 1e-9) * peak
    data = (x * 32767).astype("<i2").tobytes()
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)
    print("wrote", path, f"{len(x) / SR:.2f}s")


if __name__ == "__main__":
    write("S_WhistleShort", whistle(0.32))
    write("S_WhistleLong", whistle(1.3))
    write("S_WhistleTriple", np.concatenate([whistle(0.3), silence(0.18), whistle(0.3), silence(0.18), whistle(1.1)]))
    write("S_CrowdLoop", make_loop(crowd(13.0)), peak=0.5)
    write("S_CrowdCheer", cheer(4.0))
    write("S_CrowdBoo", boo(3.0), peak=0.7)
    write("S_Heartbeat", heartbeat(), peak=0.9)
