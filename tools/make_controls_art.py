#!/usr/bin/env python3
# Heretic 360: draws the controller on the INFO menu's controls page (src/heretic/info360.c) and writes it as
# src/heretic/info360_art.h, in Heretic's palette, run-length coded.
#
#   python3 tools/make_controls_art.py path/to/heretic.wad
#
# The WAD gives only the palette, to pick the nearest colours; nothing of the WAD goes into the header. The controller
# is a plain one of my own drawing, shaped like a 360 pad, with no logo.

import struct
import sys

from PIL import Image, ImageDraw

SCALE = 4  # drawn this many times larger, then shrunk, for smooth edges
# The box on Heretic's 320x200 screen the drawing fills; info360.c draws it there.
BOX_X, BOX_Y, BOX_W, BOX_H = 70, 24, 180, 98

BODY = (78, 80, 86)
BODY_EDGE = (150, 152, 158)
DARK = (34, 34, 38)
STICK = (58, 58, 62)
STICK_TOP = (96, 96, 100)
SMALL = (120, 122, 128)
A_GREEN = (60, 170, 60)
B_RED = (200, 50, 40)
X_BLUE = (50, 100, 210)
Y_YELLOW = (230, 200, 50)


def read_palette(wad_path):
    with open(wad_path, 'rb') as f:
        data = f.read()
    count, offset = struct.unpack('<ii', data[4:12])
    for i in range(count):
        pos, size, name = struct.unpack('<ii8s', data[offset + 16 * i:offset + 16 * i + 16])
        if name.rstrip(b'\0') == b'PLAYPAL':
            raw = data[pos:pos + 768]
            return [tuple(raw[j * 3:j * 3 + 3]) for j in range(256)]
    raise SystemExit('no PLAYPAL in ' + wad_path)


def nearest(palette, rgb):
    best, best_d = 0, None
    for i, p in enumerate(palette):
        d = sum((a - b) ** 2 for a, b in zip(p, rgb))
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


def draw():
    # Coordinates below are on the 320x200 screen; s() scales them into the big drawing, relative to the box.
    img = Image.new('RGBA', (BOX_W * SCALE, BOX_H * SCALE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def s(*v):
        out = []
        for i, x in enumerate(v):
            out.append((x - (BOX_X if i % 2 == 0 else BOX_Y)) * SCALE)
        return out

    def ellipse(cx, cy, rx, ry, fill, outline=None, width=1):
        d.ellipse(s(cx - rx, cy - ry, cx + rx, cy + ry), fill=fill, outline=outline, width=width * SCALE)

    def rrect(x0, y0, x1, y1, r, fill, outline=None, width=1):
        d.rounded_rectangle(s(x0, y0, x1, y1), radius=r * SCALE, fill=fill, outline=outline, width=width * SCALE)

    # Triggers and bumpers, behind the body.
    rrect(110, 28, 134, 44, 5, DARK, BODY_EDGE)
    rrect(186, 28, 210, 44, 5, DARK, BODY_EDGE)
    rrect(98, 40, 142, 50, 4, STICK, BODY_EDGE)
    rrect(178, 40, 222, 50, 4, STICK, BODY_EDGE)

    # The body: an upper part and two grips, drawn twice, edge then fill, so the edge rings the whole shape.
    for colour, grow in ((BODY_EDGE, 1), (BODY, 0)):
        rrect(92 - grow, 46 - grow, 228 + grow, 96 + grow, 22, colour)
        ellipse(108, 92, 20 + grow, 28 + grow, colour)
        ellipse(212, 92, 20 + grow, 28 + grow, colour)

    # Left stick, top left; right stick, lower right; D-pad, lower left; face buttons, top right.
    for cx, cy in ((116, 66), (184, 88)):
        ellipse(cx, cy, 11, 11, DARK)
        ellipse(cx, cy, 8, 8, STICK, STICK_TOP)
        ellipse(cx, cy, 4, 4, STICK_TOP)
    ellipse(136, 88, 10, 10, DARK)
    d.rectangle(s(133, 80, 139, 96), fill=STICK)
    d.rectangle(s(128, 85, 144, 91), fill=STICK)
    for (cx, cy), colour in (((204, 57), Y_YELLOW), ((195, 66), X_BLUE), ((213, 66), B_RED), ((204, 75), A_GREEN)):
        ellipse(cx, cy, 5, 5, colour, DARK)

    # Back and Start, and the round middle button with no logo.
    ellipse(150, 66, 4, 2.5, SMALL, DARK)
    ellipse(170, 66, 4, 2.5, SMALL, DARK)
    ellipse(160, 55, 6, 6, STICK, SMALL)

    return img.resize((BOX_W, BOX_H), Image.LANCZOS)


def encode(img, palette):
    # Per byte h: h & 0x80, a run of (h & 0x7f) + 1 clear pixels; otherwise h + 1 palette indexes follow.
    px = img.load()
    cache = {}
    out = bytearray()
    pixels = []
    for y in range(BOX_H):
        for x in range(BOX_W):
            r, g, b, a = px[x, y]
            if a < 128:
                pixels.append(None)
            else:
                rgb = (r, g, b)
                if rgb not in cache:
                    cache[rgb] = nearest(palette, rgb)
                pixels.append(cache[rgb])
    i = 0
    while i < len(pixels):
        if pixels[i] is None:
            n = 1
            while i + n < len(pixels) and pixels[i + n] is None and n < 128:
                n += 1
            out.append(0x80 | (n - 1))
            i += n
        else:
            n = 1
            while i + n < len(pixels) and pixels[i + n] is not None and n < 128:
                n += 1
            out.append(n - 1)
            out.extend(pixels[i:i + n])
            i += n
    return bytes(out)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__ if __doc__ else 'usage: make_controls_art.py heretic.wad')
    palette = read_palette(sys.argv[1])
    data = encode(draw(), palette)
    lines = [
        '// Heretic 360: the controller on the INFO menu\'s controls page, written by tools/make_controls_art.py.',
        '// Run-length coded indexes into Heretic\'s palette; info360.c draws it.',
        '',
        '#define CONTROLS_ART_X %d' % BOX_X,
        '#define CONTROLS_ART_Y %d' % BOX_Y,
        '#define CONTROLS_ART_W %d' % BOX_W,
        '#define CONTROLS_ART_H %d' % BOX_H,
        '',
        'static const unsigned char controls_art[%d] = {' % len(data),
    ]
    for j in range(0, len(data), 24):
        lines.append('    ' + ', '.join('%d' % b for b in data[j:j + 24]) + ',')
    lines.append('};')
    with open('src/heretic/info360_art.h', 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('src/heretic/info360_art.h: %d bytes for %dx%d' % (len(data), BOX_W, BOX_H))


if __name__ == '__main__':
    main()
