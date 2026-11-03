"""Synthesise the decompression gas burst played on game start and raid entry.

Writes a 48 kHz mono 16-bit WAV, "pfvvshhhh": a seal clunk and pressure pop,
a low fluttering rush whose band climbs into a bright hiss, then a long airy
tail. The raid entry plays it a little higher (Marau6Audio). Seeded, so the
output is reproducible. Usage: python -I scripts/Generate-Decompression.py OUT.wav
"""
import sys
import wave

import numpy as np
from scipy.signal import butter, sosfilt

RATE = 48000
SECONDS = 2.4
rng = np.random.default_rng(6)
t = np.arange(int(RATE * SECONDS)) / RATE


def band(signal, low, high, order=4):
    if low is None:
        sos = butter(order, high, btype="lowpass", fs=RATE, output="sos")
    elif high is None:
        sos = butter(order, low, btype="highpass", fs=RATE, output="sos")
    else:
        sos = butter(order, [low, high], btype="bandpass", fs=RATE, output="sos")
    return sosfilt(sos, signal)


def norm(signal):
    return signal / np.abs(signal).max()


# "p": the seal lets go with a dull metallic clunk and a pressure pop.
clunk = sum(np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, d in ((180, 0.05), (410, 0.03), (1130, 0.012)))
pop = band(rng.standard_normal(t.size), 80, 1500) * np.exp(-t / 0.018)

# "fvv": a low rush that flutters as the gas first forces its way through.
onset = 1 - np.exp(-np.maximum(t - 0.02, 0) / 0.03)
flutter = 1 + 0.35 * np.sin(2 * np.pi * np.cumsum(32 - 20 * np.minimum(t / 0.6, 1)) / RATE)
rush = band(rng.standard_normal(t.size), 120, 900) * onset * np.exp(-t / 0.35) * flutter

# "shhhh": the hiss opens up and brightens, then thins out over the tail.
# Crossfade three fixed bands instead of a time-varying filter: low-mid to bright.
noise = rng.standard_normal(t.size)
mid = band(noise, 700, 2500)
high = band(noise, 2500, 7000)
air = band(noise, 6000, 14000)
bright = np.clip((t - 0.05) / 0.35, 0, 1)
hiss_env = onset * (np.exp(-t / 0.9) * 0.8 + 0.2 * np.exp(-t / 0.25)) * (1 - np.exp(-t / 0.08))
hiss = (norm(mid) * (1 - 0.7 * bright) + norm(high) * (0.4 + 0.6 * bright) + 0.5 * norm(air) * bright) * hiss_env
# Slow turbulence so the jet does not sound like a static noise floor.
wobble = 1 + 0.12 * band(rng.standard_normal(t.size), None, 9, order=2) / 0.02
hiss *= np.clip(wobble, 0.6, 1.4)

gas = 0.55 * norm(clunk * np.exp(-t / 0.08)) + 0.5 * norm(pop) + 0.6 * norm(rush) + 0.75 * norm(hiss)

# Soft clip, 3 ms fade-in and a 150 ms fade-out against clicks, normalise to -3 dBFS.
gas = np.tanh(1.3 * gas)
fade_in, fade_out = int(0.003 * RATE), int(0.15 * RATE)
gas[:fade_in] *= np.linspace(0, 1, fade_in)
gas[-fade_out:] *= np.linspace(1, 0, fade_out)
gas *= 10 ** (-3 / 20) / np.abs(gas).max()

with wave.open(sys.argv[1], "wb") as out:
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(RATE)
    out.writeframes((gas * 32767).astype("<i2").tobytes())
