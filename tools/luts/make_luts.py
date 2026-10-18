#!/usr/bin/env python3
"""make_luts.py OUT_DIR : writes the engine's example colour LUTs (ENG-F10, kke/ColorLut.h) as 17^3 .cube files.

teal_orange    shadows towards teal, light towards orange, greys kept neutral in the middle (the film look).
bleach_bypass  half the colour gone and more contrast, as when a film print skips the bleach bath.
Both are made from formulas here, so they are ours to ship (no licence to track)."""
import colorsys, os, sys

N = 17


def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3.0 - 2.0 * x)


def teal_orange(r, g, b):
    lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
    # Teal in the shadows, orange in the light, fading to nothing at mid grey.
    t = smooth(lum * 1.25) - smooth((1.0 - lum) * 1.25)  # -1 shadows .. +1 light
    shift = (0.10 * t, 0.02 * t, -0.10 * t) if t > 0 else (0.08 * t, -0.01 * t, -0.04 * t)
    out = [min(max(c + s * (1.0 - abs(2.0 * lum - 1.0) * 0.3), 0.0), 1.0) for c, s in zip((r, g, b), shift)]
    # A little more saturation in the mids, where faces are.
    h, l, s = colorsys.rgb_to_hls(*out)
    return colorsys.hls_to_rgb(h, l, min(s * (1.0 + 0.15 * (1.0 - abs(2.0 * l - 1.0))), 1.0))


def bleach_bypass(r, g, b):
    lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
    c = [lum + (x - lum) * 0.45 for x in (r, g, b)]
    return [min(max(0.5 + (x - 0.5) * 1.25, 0.0), 1.0) for x in c]


def write(path, title, fn):
    with open(path, "w") as f:
        f.write("# made by tools/luts/make_luts.py\nTITLE \"%s\"\nLUT_3D_SIZE %d\n" % (title, N))
        for b in range(N):
            for g in range(N):
                for r in range(N):
                    out = fn(r / (N - 1), g / (N - 1), b / (N - 1))
                    f.write("%.6f %.6f %.6f\n" % tuple(out))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/luts"
    os.makedirs(out, exist_ok=True)
    write(os.path.join(out, "teal_orange.cube"), "Teal and orange", teal_orange)
    write(os.path.join(out, "bleach_bypass.cube"), "Bleach bypass", bleach_bypass)
