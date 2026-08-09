#!/usr/bin/env python3
"""Erase the painted muzzle flame from GTJ3D's viewmodel sprite sheets.

The game draws its own muzzle flash now, so the flame baked into frames 1..n
has to go -- but the rest of those frames (slide travel, pump action, hand
movement) must stay. So each frame is diffed against frame 0 and only pixels
that are BOTH fire-coloured AND new relative to frame 0 are removed; they are
replaced with whatever frame 0 has there, which is usually transparency.

The hand is a dull orange-brown and barely changes between frames, so the
"fire-coloured AND changed" pair leaves it alone.

Run from tools/stage_assets.py, or standalone:
    python strip_muzzle_fire.py
"""

import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEAPONS = os.path.join(ROOT, 'assets', 'weapons')

SHEETS = [
    ('spr_handgun_%d.png', 5),
    ('spr_assault_rifle_%d.png', 2),
    ('spr_shotgun_%d.png', 7),
    ('spr_rocket_launcher_%d.png', 16),
]


def read_png(path):
    d = open(path, 'rb').read()
    i, idat, w, h, ctype = 8, b'', 0, 0, 6
    while i < len(d):
        ln = struct.unpack_from('>I', d, i)[0]
        tag = d[i + 4:i + 8]
        body = d[i + 8:i + 8 + ln]
        if tag == b'IHDR':
            w, h, _, ctype = struct.unpack_from('>IIBB', body, 0)
        elif tag == b'IDAT':
            idat += body
        i += 12 + ln
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(w * h * bpp)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + stride]); pos += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bpp, out


def write_png(path, w, h, rgba):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


def to_rgba(w, h, bpp, px):
    if bpp == 4:
        return bytearray(px)
    out = bytearray(w * h * 4)
    for i in range(w * h):
        out[i * 4:i * 4 + 3] = px[i * 3:i * 3 + 3]
        out[i * 4 + 3] = 255
    return out


def is_fire_core(r, g, b):
    """Unmistakable flame: bright, saturated, red or yellow dominant."""
    mx, mn = max(r, g, b), min(r, g, b)
    if mx < 150:
        return False
    sat = (mx - mn) / float(mx)
    return r >= g and r > b + 55 and sat > 0.45


def is_fire_edge(r, g, b):
    """The dull tan fringe around the flame. Only trusted next to a core
    pixel, because on its own this also matches skin."""
    mx = max(r, g, b)
    return mx > 110 and r >= b and (r - b) > 22


def main():
    if not os.path.isdir(WEAPONS):
        print('no assets/weapons directory; run stage_assets.py first')
        return
    total_frames = total_px = 0
    for pattern, frames in SHEETS:
        base_path = os.path.join(WEAPONS, pattern % 0)
        if not os.path.exists(base_path):
            continue
        bw, bh, bbpp, bpx = read_png(base_path)
        base = to_rgba(bw, bh, bbpp, bpx)

        for i in range(1, frames):
            p = os.path.join(WEAPONS, pattern % i)
            if not os.path.exists(p):
                continue
            w, h, bpp, px = read_png(p)
            if (w, h) != (bw, bh):
                continue
            cur = to_rgba(w, h, bpp, px)

            # "New relative to frame 0": frame 0 is empty here, or this pixel
            # clearly lit up. The hand barely changes between frames, so it
            # never qualifies -- which is what keeps it safe from the flood
            # fill below.
            def is_new(o, r, g, b):
                ba = base[o + 3]
                if ba < 8:
                    return True
                br, bb = base[o], base[o + 2]
                return (r - br > 30) or ((r - b) - (br - bb) > 30)

            # Pass 1: seed from unmistakable flame pixels.
            mark = bytearray(w * h)
            stack = []
            for k in range(w * h):
                o = k * 4
                if cur[o + 3] < 8:
                    continue
                r, g, b = cur[o], cur[o + 1], cur[o + 2]
                if is_fire_core(r, g, b) and is_new(o, r, g, b):
                    mark[k] = 1
                    stack.append(k)

            # Pass 2: grow into the dull fringe, but only through pixels that
            # are also new. This catches the tan outer cone the core test
            # misses without ever reaching the (unchanged) hand.
            while stack:
                k = stack.pop()
                kx, ky = k % w, k // w
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = kx + dx, ky + dy
                    if nx < 0 or ny < 0 or nx >= w or ny >= h:
                        continue
                    nk = ny * w + nx
                    if mark[nk]:
                        continue
                    o = nk * 4
                    if cur[o + 3] < 8:
                        continue
                    r, g, b = cur[o], cur[o + 1], cur[o + 2]
                    if is_fire_edge(r, g, b) and is_new(o, r, g, b):
                        mark[nk] = 1
                        stack.append(nk)

            removed = 0
            for k in range(w * h):
                if not mark[k]:
                    continue
                o = k * 4
                cur[o] = base[o]; cur[o + 1] = base[o + 1]
                cur[o + 2] = base[o + 2]; cur[o + 3] = base[o + 3]
                removed += 1

            if removed:
                write_png(p, w, h, cur)
                total_frames += 1
                total_px += removed

    print('muzzle fire stripped from %d frame(s), %d pixels cleared'
          % (total_frames, total_px))


if __name__ == '__main__':
    main()
