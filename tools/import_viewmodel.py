#!/usr/bin/env python3
"""Turn a rendered weapon image into a game-ready viewmodel sprite.

The source art is a square render with a soft vignette behind the gun and
arms. That background has to become transparency, which a plain colour key
cannot do -- the vignette runs from near-black at the top to warm orange at the
bottom, and the orange is close to the skin tone of the arms.

Instead the background is found by flooding inward from the image border,
admitting a pixel only when it is close in colour to the background pixel it
came from. The artwork has dark outlines around the gun and arms, so the flood
stops there. The result is cropped to the subject and scaled to a size the game
draws at 1:1.

    python import_viewmodel.py <src.png> <dst.png> [--tol 22] [--height 260]
"""

import collections
import os
import struct
import sys
import zlib


def read_png(path):
    d = open(path, 'rb').read()
    i, idat, w, h, ctype, bitdepth = 8, b'', 0, 0, 6, 8
    while i < len(d):
        ln = struct.unpack_from('>I', d, i)[0]
        tag = d[i + 4:i + 8]
        body = d[i + 8:i + 8 + ln]
        if tag == b'IHDR':
            w, h, bitdepth, ctype = struct.unpack_from('>IIBB', body, 0)
        elif tag == b'IDAT':
            idat += body
        i += 12 + ln
    if bitdepth != 8:
        raise SystemExit('only 8-bit PNGs supported (got %d)' % bitdepth)
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
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

    # normalise to RGBA
    rgba = bytearray(w * h * 4)
    for k in range(w * h):
        o, q = k * bpp, k * 4
        if bpp == 4:
            rgba[q:q + 4] = out[o:o + 4]
        elif bpp == 3:
            rgba[q:q + 3] = out[o:o + 3]; rgba[q + 3] = 255
        elif bpp == 2:
            rgba[q] = rgba[q + 1] = rgba[q + 2] = out[o]; rgba[q + 3] = out[o + 1]
        else:
            rgba[q] = rgba[q + 1] = rgba[q + 2] = out[o]; rgba[q + 3] = 255
    return w, h, rgba


def write_png(path, w, h, rgba):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


def key_background(w, h, px, tol):
    """Flood the vignette inward from the border; returns a bytearray mask.

    A colour-continuity walk alone leaks: the gun's own surfaces are just as
    smooth as the vignette, so once the flood slips through a soft edge it eats
    the whole subject. So the artwork's outlines are turned into barriers
    first -- any pixel whose local luminance gradient is strong is off limits --
    and the flood is only allowed through flat, background-coloured ground.
    Barrier pixels themselves stay opaque, which keeps the outlines intact.
    """
    lum = bytearray(w * h)
    for k in range(w * h):
        o = k * 4
        lum[k] = (px[o] * 77 + px[o + 1] * 150 + px[o + 2] * 29) >> 8

    barrier = bytearray(w * h)
    for y in range(1, h - 1):
        row = y * w
        for x in range(1, w - 1):
            k = row + x
            g = abs(lum[k + 1] - lum[k - 1]) + abs(lum[k + w] - lum[k - w])
            if g > tol:
                barrier[k] = 1

    bg = bytearray(w * h)
    q = collections.deque()

    def push(k):
        if not bg[k] and not barrier[k]:
            bg[k] = 1
            q.append(k)

    # Seeded from the top and sides only. The arms run down to the bottom of
    # the frame and fade into the warm part of the vignette with no outline
    # between them, so a seed on the bottom row lands inside an arm and the
    # flood hollows it out. The lower corners are still reached by walking down
    # the left and right edges, which are background all the way.
    for x in range(w):
        push(x)
    for y in range(h):
        push(y * w)
        push(y * w + w - 1)

    while q:
        k = q.popleft()
        kx, ky = k % w, k // w
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = kx + dx, ky + dy
            if nx < 0 or ny < 0 or nx >= w or ny >= h:
                continue
            nk = ny * w + nx
            if bg[nk] or barrier[nk]:
                continue
            bg[nk] = 1
            q.append(nk)

    # Grow one pixel into the barrier ring so the keyed edge is not haloed by
    # leftover background colour, but only where it touches open background.
    grown = bytearray(bg)
    for y in range(1, h - 1):
        row = y * w
        for x in range(1, w - 1):
            k = row + x
            if bg[k] or not barrier[k]:
                continue
            if bg[k - 1] or bg[k + 1] or bg[k - w] or bg[k + w]:
                o = k * 4
                # only if it still looks like background, not like ink
                if lum[k] < 40 or lum[k] > 215:
                    grown[k] = 1
    return grown


def despeckle(w, h, bg):
    """Fill single-pixel holes so the alpha edge is not noisy."""
    out = bytearray(bg)
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            k = y * w + x
            if bg[k]:
                continue
            n = (bg[k - 1] + bg[k + 1] + bg[k - w] + bg[k + w])
            if n == 4:
                out[k] = 1
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    tol = 22
    target_h = 260
    for i, a in enumerate(sys.argv):
        if a == '--tol' and i + 1 < len(sys.argv):
            tol = int(sys.argv[i + 1])
        if a == '--height' and i + 1 < len(sys.argv):
            target_h = int(sys.argv[i + 1])

    w, h, px = read_png(src)
    bg = despeckle(w, h, key_background(w, h, px, tol))

    for k in range(w * h):
        if bg[k]:
            px[k * 4 + 3] = 0

    # crop to the subject
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = y * w
        for x in range(w):
            if px[(row + x) * 4 + 3] > 8:
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    if x1 < x0:
        raise SystemExit('everything was keyed out; lower --tol')
    cw, ch = x1 - x0 + 1, y1 - y0 + 1

    # box-filter downscale, weighting colour by alpha so edges do not darken
    scale = target_h / float(ch)
    nw, nh = max(1, int(round(cw * scale))), max(1, int(round(ch * scale)))
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        sy0 = y0 + int(y * ch / nh)
        sy1 = max(sy0 + 1, y0 + int((y + 1) * ch / nh))
        for x in range(nw):
            sx0 = x0 + int(x * cw / nw)
            sx1 = max(sx0 + 1, x0 + int((x + 1) * cw / nw))
            ar = ag = ab = aa = 0.0
            n = 0
            for sy in range(sy0, sy1):
                for sx in range(sx0, sx1):
                    o = (sy * w + sx) * 4
                    a = px[o + 3] / 255.0
                    ar += px[o] * a; ag += px[o + 1] * a; ab += px[o + 2] * a
                    aa += a
                    n += 1
            q = (y * nw + x) * 4
            if aa > 0.001:
                out[q] = min(255, int(ar / aa))
                out[q + 1] = min(255, int(ag / aa))
                out[q + 2] = min(255, int(ab / aa))
            out[q + 3] = int(255.0 * aa / max(n, 1))

    write_png(dst, nw, nh, out)
    kept = sum(1 for k in range(nw * nh) if out[k * 4 + 3] > 8)
    print('%s -> %s  %dx%d (from %dx%d crop of %dx%d), %d opaque px'
          % (os.path.basename(src), dst, nw, nh, cw, ch, w, h, kept))
    print('  suggested origin: x=%d  y=%d   (vmScale 1.0)' % (nw // 2, nh - 256))


if __name__ == '__main__':
    main()
