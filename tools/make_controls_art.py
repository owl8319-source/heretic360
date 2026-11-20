#!/usr/bin/env python3
# Heretic 360: draws the controller on the INFO menu's controls page (src/heretic/info360.c) and writes it as
# src/heretic/info360_art.h, in Heretic's palette, run-length coded.
#
#   python3 tools/make_controls_art.py path/to/heretic.wad
#       draws a plain controller of its own (no logo) into src/heretic/info360_art.h, the one in the repository.
#   python3 tools/make_controls_art.py path/to/heretic.wad picture.jpg
#       uses a picture of a controller instead (on a white background), into src/heretic/info360_art_local.h. That
#       file is left out of git (.gitignore): a picture from the web isn't ours to publish. When it's there, the build
#       uses it in place of info360_art.h.
#
# The WAD gives only the palette, to pick the nearest colours; nothing of the WAD goes into the header.

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


# Room for a picture: between the page's title and the list under it, centred.
PICTURE_TOP, PICTURE_BOTTOM = 22, 123


def picture(path):
    # The white around the controller becomes clear: every near-white pixel joined to the picture's edge. White
    # inside the controller stays.
    src = Image.open(path).convert('RGB')
    w, h = src.size
    px = src.load()

    def white(x, y):
        return min(px[x, y]) >= 232

    clear = bytearray(w * h)
    todo = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    while todo:
        x, y = todo.pop()
        if clear[y * w + x] or not white(x, y):
            continue
        clear[y * w + x] = 1
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and not clear[ny * w + nx]:
                todo.append((nx, ny))

    img = src.convert('RGBA')
    ipx = img.load()
    for y in range(h):
        for x in range(w):
            if clear[y * w + x]:
                ipx[x, y] = (255, 255, 255, 0)
    img = img.crop(img.getbbox())

    global BOX_X, BOX_Y, BOX_W, BOX_H
    BOX_H = PICTURE_BOTTOM - PICTURE_TOP
    BOX_W = round(img.size[0] * BOX_H / img.size[1])
    BOX_X = 160 - BOX_W // 2
    BOX_Y = PICTURE_TOP
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
    if len(sys.argv) not in (2, 3):
        raise SystemExit('usage: make_controls_art.py heretic.wad [picture]')
    palette = read_palette(sys.argv[1])
    if len(sys.argv) == 3:
        art, out = picture(sys.argv[2]), 'src/heretic/info360_art_local.h'
    else:
        art, out = draw(), 'src/heretic/info360_art.h'
    data = encode(art, palette)
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
    with open(out, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('%s: %d bytes for %dx%d at %d,%d' % (out, len(data), BOX_W, BOX_H, BOX_X, BOX_Y))


if __name__ == '__main__':
    main()
