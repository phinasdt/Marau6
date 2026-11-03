"""Synthesise the rifle report layered over the shot sound.

Writes a 48 kHz mono 16-bit WAV: a sharp supersonic crack, a noisy muzzle
body, a low chest thump and a short tail with two slapback echoes. Seeded, so
the output is reproducible. Usage: python -I scripts/Generate-RifleShot.py OUT.wav
"""
import sys
import wave

import numpy as np
from scipy.signal import butter, sosfilt

RATE = 48000
SECONDS = 0.9
rng = np.random.default_rng(6)
t = np.arange(int(RATE * SECONDS)) / RATE


def band(signal, low, high):
    if low is None:
        sos = butter(4, high, btype="lowpass", fs=RATE, output="sos")
    elif high is None:
        sos = butter(4, low, btype="highpass", fs=RATE, output="sos")
    else:
        sos = butter(4, [low, high], btype="bandpass", fs=RATE, output="sos")
    return sosfilt(sos, signal)


noise = rng.standard_normal(t.size)
crack = band(noise, 2500, None) * np.exp(-t / 0.0025)
body = band(noise, 200, 4000) * np.exp(-t / 0.028)
sweep = 2 * np.pi * np.cumsum(55 + 75 * np.exp(-t / 0.03)) / RATE
thump = np.sin(sweep) * np.exp(-t / 0.07)
tail = band(rng.standard_normal(t.size), None, 900) * np.exp(-t / 0.22)

shot = 1.0 * crack / np.abs(crack).max() + 0.8 * body / np.abs(body).max() \
    + 0.9 * thump + 0.35 * tail / np.abs(tail).max()
for delay, gain in ((0.085, 0.22), (0.15, 0.12)):
    offset = int(delay * RATE)
    shot[offset:] += gain * band(shot[:-offset], None, 2500)

# Soft clip for weight, a 2 ms fade-in against clicks, then normalise to -1 dBFS.
shot = np.tanh(1.6 * shot)
shot[: int(0.002 * RATE)] *= np.linspace(0, 1, int(0.002 * RATE))
shot *= 10 ** (-1 / 20) / np.abs(shot).max()

with wave.open(sys.argv[1], "wb") as out:
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(RATE)
    out.writeframes((shot * 32767).astype("<i2").tobytes())
