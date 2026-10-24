#!/usr/bin/env python3
"""Generate HIVEBREAKERS' 2D art.

Usage: tools/genart.py OUT_DIR

Writes, all original and drawn here procedurally:
  OUT_DIR/hive.fnt + hive_0.png  heading font (BMFont text format): a chunky
                                 5x7 pixel face at 2x with a 1 px outline,
                                 white so the game tints it per text style
  OUT_DIR/logo.png               the HIVEBREAKERS logo: the same face at 4x,
                                 amber-to-red gradient, outline, drop shadow
  OUT_DIR/planet.png             a shaded alien planet for the briefing screen

The build converts them with libdragon's mkfont and mksprite. Standard
library only, deterministic.
"""

import math
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------------------
# PNG writer (RGBA, 8 bits per channel)

def write_png(path, w, h, pixels):
    """pixels: list of h rows, each a list of w (r, g, b, a) tuples."""
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def canvas(w, h):
    return [[(0, 0, 0, 0) for _ in range(w)] for _ in range(h)]


# ---------------------------------------------------------------------------
# The face: 5x7 glyphs

GLYPHS = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
    "-": [".....", ".....", ".....", ".###.", ".....", ".....", "....."],
    ":": [".....", "..#..", ".....", ".....", ".....", "..#..", "....."],
    ".": [".....", ".....", ".....", ".....", ".....", ".....", "..#.."],
    ",": [".....", ".....", ".....", ".....", ".....", "..#..", ".#..."],
    "!": ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
    "?": [".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."],
    "/": ["....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."],
    "'": ["..#..", "..#..", ".....", ".....", ".....", ".....", "....."],
    "%": ["##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##"],
    "+": [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."],
    "(": ["...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."],
    ")": [".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."],
    "#": [".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."],
    "<": ["...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."],
    ">": [".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."],
    "*": [".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."],
    "=": [".....", ".....", "#####", ".....", "#####", ".....", "....."],
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
}

OUTLINE = (24, 18, 28, 255)


def render_glyphs(text, scale, outline, fill_fn, gap=1):
    """Lay out `text` in the face. fill_fn(x, y, h) gives the fill colour of
    a pixel at row y of a glyph image h pixels tall. Returns (w, h, rows)."""
    cols = len(text) * (5 + gap) - gap
    w, h = cols * scale + 2 * outline, 7 * scale + 2 * outline
    mask = [[False] * w for _ in range(h)]
    for k, ch in enumerate(text):
        g = GLYPHS[ch]
        for gy, line in enumerate(g):
            for gx, c in enumerate(line):
                if c != "#":
                    continue
                x0 = outline + (k * (5 + gap) + gx) * scale
                y0 = outline + gy * scale
                for yy in range(scale):
                    for xx in range(scale):
                        mask[y0 + yy][x0 + xx] = True
    img = canvas(w, h)
    for y in range(h):
        for x in range(w):
            if mask[y][x]:
                img[y][x] = fill_fn(x, y, h)
            elif any(mask[yy][xx] for yy in range(max(0, y - outline), min(h, y + outline + 1))
                     for xx in range(max(0, x - outline), min(w, x + outline + 1))):
                img[y][x] = OUTLINE
    return w, h, img


# ---------------------------------------------------------------------------

def make_font(out):
    """Every glyph in one atlas row, white with a dark outline."""
    scale, outline, cell_w, cell_h = 2, 1, 12, 16
    chars = sorted(GLYPHS)
    w = cell_w * len(chars)
    atlas = canvas(w, cell_h)
    lines = []
    for k, ch in enumerate(chars):
        gw, gh, img = render_glyphs(ch, scale, outline, lambda x, y, h: (255, 255, 255, 255))
        for y in range(gh):
            for x in range(gw):
                atlas[y][k * cell_w + x] = img[y][x]
        lines.append("char id=%d x=%d y=0 width=%d height=%d xoffset=0 yoffset=0 xadvance=%d page=0 chnl=15"
                     % (ord(ch), k * cell_w, gw, gh, cell_w))
        if ch.isalpha():  # lower case maps to the same glyphs
            lines.append("char id=%d x=%d y=0 width=%d height=%d xoffset=0 yoffset=0 xadvance=%d page=0 chnl=15"
                         % (ord(ch.lower()), k * cell_w, gw, gh, cell_w))
    write_png(os.path.join(out, "hive_0.png"), w, cell_h, atlas)
    with open(os.path.join(out, "hive.fnt"), "w") as f:
        f.write('info face="Hive" size=16 bold=0 italic=0 charset="" unicode=1 stretchH=100 smooth=0 aa=1 '
                'padding=0,0,0,0 spacing=0,0\n')
        f.write("common lineHeight=18 base=15 scaleW=%d scaleH=%d pages=1 packed=0\n" % (w, cell_h))
        f.write('page id=0 file="hive_0.png"\n')
        f.write("chars count=%d\n" % len(lines))
        f.write("\n".join(lines) + "\n")


def make_logo(out):
    top, bottom = (255, 226, 96), (214, 64, 26)

    def grad(x, y, h):
        t = max(0.0, min(1.0, (y - 2) / (h - 5)))
        c = [int(a + (b - a) * t) for a, b in zip(top, bottom)]
        if (y - 2) % 4 == 0:  # a lighter top edge on each pixel row: a little bevel
            c = [min(255, v + 30) for v in c]
        return (c[0], c[1], c[2], 255)

    w, h, img = render_glyphs("HIVEBREAKERS", 4, 2, grad)
    shadow = 3
    out_img = canvas(w + shadow, h + shadow)
    for y in range(h):
        for x in range(w):
            if img[y][x][3]:
                out_img[y + shadow][x + shadow] = (8, 4, 12, 255)
    for y in range(h):
        for x in range(w):
            if img[y][x][3]:
                out_img[y][x] = img[y][x]
    write_png(os.path.join(out, "logo.png"), w + shadow, h + shadow, out_img)


def value_noise(seed, size):
    """Tileable value noise on a size x size grid."""
    vals = []
    s = seed
    for _ in range(size * size):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        vals.append(s / 0x7FFFFFFF)

    def at(x, y):
        x0, y0 = int(math.floor(x)), int(math.floor(y))
        fx, fy = x - x0, y - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

        def v(i, j):
            return vals[(j % size) * size + (i % size)]
        top = v(x0, y0) + (v(x0 + 1, y0) - v(x0, y0)) * fx
        bot = v(x0, y0 + 1) + (v(x0 + 1, y0 + 1) - v(x0, y0 + 1)) * fx
        return top + (bot - top) * fy
    return at


def make_planet(out):
    n = 96
    r = n / 2 - 3
    noise = value_noise(7, 16)
    light = (-0.55, -0.45, 0.70)
    ll = math.sqrt(sum(c * c for c in light))
    light = tuple(c / ll for c in light)
    img = canvas(n, n)
    for y in range(n):
        for x in range(n):
            dx, dy = (x + 0.5 - n / 2) / r, (y + 0.5 - n / 2) / r
            d2 = dx * dx + dy * dy
            if d2 > 1.12:
                continue
            if d2 > 1.0:  # thin atmosphere rim
                a = int(255 * (1.12 - d2) / 0.12 * 0.6)
                img[y][x] = (255, 150, 90, a)
                continue
            dz = math.sqrt(1 - d2)
            # Latitude bands warped by noise, like a dusty desert world.
            u = math.atan2(dx, dz) * 2.0 + 3.0
            v = dy * 4.0
            band = 0.5 + 0.5 * math.sin(v * 2.2 + 2.5 * noise(u * 2.0, v * 1.5))
            spot = noise(u * 4.0 + 7, v * 4.0 + 3)
            base = (150 + 70 * band, 85 + 50 * band, 50 + 25 * band)
            if spot > 0.78:  # dark hive scars
                base = (base[0] * 0.45, base[1] * 0.35, base[2] * 0.5)
            lit = max(0.0, dx * light[0] + dy * light[1] + dz * light[2])
            shade = 0.18 + 0.82 * lit
            rim = (1 - dz) ** 3 * 0.5
            c = tuple(int(min(255, v * shade + 255 * rim * k)) for v, k in zip(base, (1.0, 0.6, 0.4)))
            img[y][x] = (c[0], c[1], c[2], 255)
    write_png(os.path.join(out, "planet.png"), n, n, img)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    make_font(out)
    make_logo(out)
    make_planet(out)


if __name__ == "__main__":
    main()
