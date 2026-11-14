# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later
"""make_icon.py -- renders the Codec pack icon: a squircle tile (#9b59d0 at the top to #6a2fb0 at the bottom, the
   tile of Phorepoc's app icons) with a white play triangle over a sound wave ("video and audio formats"). Writes
   codecpack_icon.svg          the SVG source (original artwork);
   codecpack_icon.png          256x256, the Catalogue listing's icon (index listings/codecpack-*/icon.png).
Run: py make_icon.py   (needs Pillow).
"""
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
TOP, BOTTOM, WHITE = (0x9B, 0x59, 0xD0), (0x6A, 0x2F, 0xB0), (255, 255, 255)
N = 4.6                                                     # the tile is a squircle: |x|^N + |y|^N = 1
CX, CY, R = 32.0, 32.0, 30.0

PLAY = [(24, 13), (24, 39), (45, 26)]                       # the play triangle (64x64 box)
WAVE = [(12, 50, 4), (18, 50, 8), (24, 50, 5), (30, 50, 10), (36, 50, 6), (42, 50, 9), (48, 50, 4)]   # bars: x, centre y, half height
BAR = 3.2                                                   # bar width


def squircle(points=240):
    pts = []
    for i in range(points):
        t = 2 * math.pi * i / points
        c, s = math.cos(t), math.sin(t)
        pts.append((CX + R * math.copysign(abs(c) ** (2 / N), c), CY + R * math.copysign(abs(s) ** (2 / N), s)))
    return pts


def svg():
    path = "M" + "L".join("%.2f %.2f" % p for p in squircle()) + "Z"
    play = "M" + "L".join("%g %g" % p for p in PLAY) + "Z"
    bars = "\n".join('  <rect x="%.1f" y="%g" width="%.1f" height="%g" rx="1.6" fill="#FFFFFF"/>' % (x - BAR / 2, y - h, BAR, 2 * h)
                     for x, y, h in WAVE)
    return """<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <!-- Codec pack icon: original artwork, EPL-2.0 OR GPL-2.0-or-later (The Phorepoc Authors). The tile is the squircle
       |x|^4.6 + |y|^4.6 = 1, written out as a path by make_icon.py -->
  <defs>
    <linearGradient id="g" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#9b59d0"/>
      <stop offset="1" stop-color="#6a2fb0"/>
    </linearGradient>
  </defs>
  <path d="%s" fill="url(#g)"/>
  <path d="%s" fill="#FFFFFF"/>
%s
</svg>
""" % (path, play, bars)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t + 0.5) for i in range(3))


def render(size):
    k = 4                                                   # supersampling
    s = size * k / 64.0
    big = Image.new("RGBA", (size * k, size * k), (0, 0, 0, 0))
    grad = Image.new("RGBA", big.size)
    gd = ImageDraw.Draw(grad)
    for y in range(big.size[1]):
        gd.line([(0, y), (big.size[0], y)], fill=lerp(TOP, BOTTOM, y / (big.size[1] - 1)) + (255,))
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).polygon([(x * s, y * s) for x, y in squircle()], fill=255)
    big.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(big)
    d.polygon([(x * s, y * s) for x, y in PLAY], fill=WHITE)
    for x, y, h in WAVE:
        d.rounded_rectangle([(x - BAR / 2) * s, (y - h) * s, (x + BAR / 2) * s, (y + h) * s], radius=1.6 * s, fill=WHITE)
    return big.resize((size, size), Image.LANCZOS)


def main():
    open(os.path.join(HERE, "codecpack_icon.svg"), "w", newline="\n").write(svg())
    render(256).save(os.path.join(HERE, "codecpack_icon.png"))


if __name__ == "__main__":
    main()
