"""Synthesize the station instruments and effects as mono PCM."""
from array import array
from math import exp, pi, sin, tanh
from pathlib import Path
import random
import sys
import wave

OUT = Path(__file__).resolve().parents[1] / "assets/audio"
RATE = 16000


def write(name, duration, instrument):
    rng = random.Random(319)
    frames = array("h")
    for i in range(round(RATE * duration)):
        t = i / RATE
        envelope = min(1, t * 200, (duration - t) * 40)
        frames.append(round(19000 * envelope * tanh(instrument(t, rng.uniform(-1, 1)))))
    if sys.byteorder != "little":
        frames.byteswap()
    with wave.open(str(OUT / (name + ".wav")), "wb") as out:
        out.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        out.writeframes(frames.tobytes())


def tone(hz, t):
    return sin(2 * pi * hz * t)


def bell(t, noise):
    return sum(gain * exp(-fall * t) * tone(146 * ratio, t)
               for ratio, gain, fall in [(1, .8, 1.4), (2.39, .29, 2.1), (3.01, .17, 3), (4.12, .1, 5)])


OUT.mkdir(parents=True, exist_ok=True)
write("fog-bell", 2.4, bell)
write("chime", 1.7, lambda t, n: exp(-3*t) * (tone(293.66, t) + .25 * tone(883, t)))
write("drone", 4, lambda t, n: .3 * tone(55, t) + .15 * tone(82.5, t) + .09 * tone(111, t))
write("pulse", .35, lambda t, n: exp(-15*t) * tone(54 + 20 * exp(-28*t), t))
write("swing", .32, lambda t, n: sin(pi*t/.32)**2 * (.5*n + .3*tone(360-640*t,t)))
write("harpoon", .24, lambda t, n: exp(-24*t) * (n*.6 + tone(104-120*t,t)))
write("fire", .45, lambda t, n: exp(-4*t) * n * (.25 + .6 * max(0, tone(17,t))**12))
write("splash", .42, lambda t, n: exp(-8*t) * (n*.5 + .28*tone(510-720*t,t)))
write("gutter", .9, lambda t, n: exp(-3*t) * (tone(140-100*t,t) + .25*n))
