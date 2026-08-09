#!/usr/bin/env python3
"""Split a vertical sprite strip on a flat key colour into numbered frames.

Used for the Doom super shotgun sheet, which is a column of frames on cyan
with thin white rules between them. Frames are found by locating the rows that
contain no subject pixels, the key colour becomes transparency, and every
frame is padded to a common canvas so the gun does not jitter between frames.

    python import_sheet.py <src.png> <dst_prefix> [--key 0,255,255] [--tol 60]
                           [--height 210]
Writes <dst_prefix>_0.png, _1.png, ...
"""

import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from import_viewmodel import read_png, write_png  # noqa: E402


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, prefix = sys.argv[1], sys.argv[2]
    key = (0, 255, 255)
    tol = 60
    target_h = 210
    for i, a in enumerate(sys.argv):
        if a == '--key' and i + 1 < len(sys.argv):
            key = tuple(int(v) for v in sys.argv[i + 1].split(','))
        if a == '--tol' and i + 1 < len(sys.argv):
            tol = int(sys.argv[i + 1])
        if a == '--height' and i + 1 < len(sys.argv):
            target_h = int(sys.argv[i + 1])

    w, h, px = read_png(src)

    def is_key(o):
        return (abs(px[o] - key[0]) + abs(px[o + 1] - key[1]) +
                abs(px[o + 2] - key[2])) <= tol

    # Already-transparent pixels and white separator rules are background too.
    # (The sheet separates its frames with fully transparent gutters, so
    # ignoring alpha here merges every frame into one band.)
    def is_bg(o):
        if px[o + 3] < 8:
            return True
        if is_key(o):
            return True
        r, g, b = px[o], px[o + 1], px[o + 2]
        return r > 235 and g > 235 and b > 235

    rows = []
    for y in range(h):
        row = y * w
        n = 0
        for x in range(w):
            if not is_bg((row + x) * 4):
                n += 1
                if n > 3:
                    break
        rows.append(n > 3)

    # Contiguous runs of subject rows are the frames.
    bands, start = [], None
    for y in range(h):
        if rows[y] and start is None:
            start = y
        elif not rows[y] and start is not None:
            if y - start > 8:
                bands.append((start, y))
            start = None
    if start is not None and h - start > 8:
        bands.append((start, h))

    print('%d frame band(s) found in %dx%d' % (len(bands), w, h))

    # Column extent across all frames, so every frame shares a canvas.
    gx0, gx1 = w, -1
    for (y0, y1) in bands:
        for y in range(y0, y1):
            row = y * w
            for x in range(w):
                if not is_bg((row + x) * 4):
                    if x < gx0: gx0 = x
                    if x > gx1: gx1 = x
    cw = gx1 - gx0 + 1
    ch = max(y1 - y0 for (y0, y1) in bands)
    scale = target_h / float(ch)
    nw, nh = max(1, int(round(cw * scale))), max(1, int(round(ch * scale)))

    for idx, (y0, y1) in enumerate(bands):
        out = bytearray(nw * nh * 4)
        bh = y1 - y0
        # bottom-align each frame on the shared canvas
        pad = ch - bh
        for y in range(nh):
            sy = y0 - pad + int(y * ch / nh)
            for x in range(nw):
                sx = gx0 + int(x * cw / nw)
                q = (y * nw + x) * 4
                if sy < y0 or sy >= y1:
                    continue
                o = (sy * w + sx) * 4
                if is_bg(o):
                    continue
                out[q] = px[o]; out[q + 1] = px[o + 1]
                out[q + 2] = px[o + 2]; out[q + 3] = 255
        dst = '%s_%d.png' % (prefix, idx)
        write_png(dst, nw, nh, out)
        print('  frame %d: rows %d..%d -> %s (%dx%d)' % (idx, y0, y1, dst, nw, nh))

    print('  suggested origin: x=%d  y=%d   (vmScale 1.0)' % (nw // 2, nh - 256))


if __name__ == '__main__':
    main()
