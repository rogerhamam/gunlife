#!/usr/bin/env python3
"""
GMK 8.0 (GameMaker 8) project extractor.

Dumps sounds, sprites, backgrounds, scripts, object event code and room layouts
out of a .gmk so they can be reused by the C++ port.

Format reference: LateralGM's GmFileReader.java / GmStreamDecoder.java.
GMK (unlike a compiled GM8 .exe) is *not* encrypted -- resources are simply
wrapped in zlib blobs.

Usage:
    python gmk_extract.py <file.gmk> <outdir>
"""

import json
import os
import struct
import sys
import zlib

# Sprites/backgrounds are stored as raw BGRA; re-encode to PNG without Pillow.
def write_png(path, width, height, bgra):
    px = bytearray(bgra)
    px[0::4], px[2::4] = px[2::4], px[0::4]   # BGRA -> RGBA
    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)                         # filter type 0 (None)
        raw += px[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', ihdr))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


class Reader:
    def __init__(self, data, name='<root>'):
        self.d = data
        self.p = 0
        self.name = name

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def f64(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]
        self.p += 8
        return v

    def boolean(self):
        return self.u32() != 0

    def blob(self, n):
        v = self.d[self.p:self.p + n]
        if len(v) != n:
            raise EOFError('%s: wanted %d bytes at %d' % (self.name, n, self.p))
        self.p += n
        return v

    def string(self):
        return self.blob(self.u32()).decode('latin-1')

    def skip(self, n):
        self.p += n

    def inflate(self):
        """GMK beginInflate: u32 length followed by a zlib stream."""
        n = self.u32()
        raw = self.blob(n)
        return Reader(zlib.decompress(raw), self.name)

    def eof(self):
        return self.p >= len(self.d)


def read_actions(r):
    """Parse an action list, returning any embedded GML code strings."""
    ver = r.u32()
    assert ver == 400, 'action list version %d' % ver
    code = []
    for _ in range(r.u32()):
        r.skip(4)                    # action version
        r.u32()                      # lib id
        r.u32()                      # action id
        r.skip(20)                   # kind, relative, question, applies, exectype
        r.skip(r.u32())              # exec function name
        r.skip(r.u32())              # exec code
        nargs = r.u32()
        argkinds = [r.u32() for _ in range(r.u32())]
        r.i32()                      # applies to
        r.boolean()                  # relative
        actual = r.u32()
        for l in range(actual):
            val = r.string()
            if l < nargs and l < len(argkinds) and argkinds[l] == 1:
                # kind 1 == ARG_STRING, which is how "execute code" stores GML
                if val.strip():
                    code.append(val)
        r.boolean()                  # "not"
    return code


# GameMaker main event numbers -> readable names
MAIN_EVENTS = {
    0: 'Create', 1: 'Destroy', 2: 'Alarm', 3: 'Step', 4: 'Collision',
    5: 'Keyboard', 6: 'Mouse', 7: 'Other', 8: 'Draw', 9: 'KeyPress',
    10: 'KeyRelease', 11: 'Trigger',
}


def main():
    src, outdir = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    r = Reader(data)

    assert r.u32() == 1234321, 'not a GMK file'
    ver = r.u32()
    assert ver == 800, 'only GMK 8.0 supported, got %d' % ver
    r.u32()          # game id
    r.blob(16)       # guid

    def mkdir(*parts):
        p = os.path.join(outdir, *parts)
        os.makedirs(p, exist_ok=True)
        return p

    summary = {}

    # ---- settings (one big zlib blob; nothing we need) ----
    assert r.u32() == 800
    r.inflate()

    # ---- triggers ----
    assert r.u32() == 800
    for _ in range(r.u32()):
        r.inflate()
    r.skip(8)

    # ---- constants ----
    assert r.u32() == 800
    consts = {}
    for _ in range(r.u32()):
        k = r.string()
        consts[k] = r.string()
    r.skip(8)
    summary['constants'] = consts

    # ---- sounds ----
    snd_dir = mkdir('sounds')
    assert r.u32() == 800
    sounds = []
    for i in range(r.u32()):
        b = r.inflate()
        if not b.boolean():
            sounds.append(None)
            continue
        name = b.string()
        b.skip(8)
        assert b.u32() == 800
        kind = b.u32()
        ftype = b.string()
        b.string()               # original file name
        payload = None
        if b.boolean():
            payload = b.blob(b.u32())
        effects = b.u32()
        volume = b.f64()
        pan = b.f64()
        b.boolean()              # preload
        ext = ftype if ftype.startswith('.') else '.' + (ftype or 'wav')
        if payload:
            with open(os.path.join(snd_dir, name + ext), 'wb') as f:
                f.write(payload)
        sounds.append({'id': i, 'name': name, 'kind': kind, 'ext': ext,
                       'volume': volume, 'pan': pan, 'effects': effects,
                       'bytes': len(payload) if payload else 0})
    summary['sounds'] = sounds

    # ---- sprites ----
    spr_dir = mkdir('sprites')
    assert r.u32() == 800
    sprites = []
    for i in range(r.u32()):
        b = r.inflate()
        if not b.boolean():
            sprites.append(None)
            continue
        name = b.string()
        b.skip(8)
        assert b.u32() == 800
        ox, oy = b.i32(), b.i32()
        frames = []
        for j in range(b.u32()):
            assert b.u32() == 800
            w, h = b.u32(), b.u32()
            if w and h:
                px = b.blob(b.u32())
                fn = '%s_%d.png' % (name, j)
                write_png(os.path.join(spr_dir, fn), w, h, px)
                frames.append({'file': fn, 'w': w, 'h': h})
        b.u32()                  # mask shape
        b.u32()                  # alpha tolerance
        b.boolean()              # separate mask
        b.u32()                  # bbox mode
        bbox = [b.i32() for _ in range(4)]
        sprites.append({'id': i, 'name': name, 'origin': [ox, oy],
                        'frames': frames, 'bbox': bbox})
    summary['sprites'] = sprites

    # ---- backgrounds ----
    bg_dir = mkdir('backgrounds')
    assert r.u32() == 800
    backgrounds = []
    for i in range(r.u32()):
        b = r.inflate()
        if not b.boolean():
            backgrounds.append(None)
            continue
        name = b.string()
        b.skip(8)
        bver = b.u32()
        assert bver == 710, 'background version %d' % bver
        b.boolean()              # use as tileset
        [b.u32() for _ in range(6)]
        assert b.u32() == 800
        w, h = b.u32(), b.u32()
        entry = {'id': i, 'name': name, 'w': w, 'h': h, 'file': None}
        if w and h:
            px = b.blob(b.u32())
            fn = name + '.png'
            write_png(os.path.join(bg_dir, fn), w, h, px)
            entry['file'] = fn
        backgrounds.append(entry)
    summary['backgrounds'] = backgrounds

    # ---- paths ----
    assert r.u32() == 800
    for _ in range(r.u32()):
        r.inflate()

    # ---- scripts ----
    scr_dir = mkdir('scripts')
    assert r.u32() == 800
    scripts = []
    for i in range(r.u32()):
        b = r.inflate()
        if not b.boolean():
            scripts.append(None)
            continue
        name = b.string()
        b.skip(8)
        assert b.u32() == 800
        code = b.string()
        with open(os.path.join(scr_dir, name + '.gml'), 'w',
                  encoding='utf-8') as f:
            f.write(code)
        scripts.append({'id': i, 'name': name, 'lines': code.count('\n') + 1})
    summary['scripts'] = scripts

    # ---- fonts ----
    fver = r.u32()
    assert fver == 800
    for _ in range(r.u32()):
        r.inflate()

    # ---- timelines ----
    tl_dir = mkdir('timelines')
    assert r.u32() == 800
    for i in range(r.u32()):
        b = r.inflate()
        if not b.boolean():
            continue
        name = b.string()
        b.skip(8)
        assert b.u32() == 500
        chunks = []
        for _ in range(b.u32()):
            step = b.u32()
            for c in read_actions(b):
                chunks.append('// === step %d ===\n%s' % (step, c))
        if chunks:
            with open(os.path.join(tl_dir, name + '.gml'), 'w',
                      encoding='utf-8') as f:
                f.write('\n\n'.join(chunks))

    # ---- objects ----
    obj_dir = mkdir('objects')
    assert r.u32() == 800
    objects = []
    for i in range(r.u32()):
        b = r.inflate()
        if not b.boolean():
            objects.append(None)
            continue
        name = b.string()
        b.skip(8)
        over = b.u32()
        assert over == 430, 'object version %d' % over
        sprite_id = b.i32()
        solid = b.boolean()
        visible = b.boolean()
        depth = b.i32()
        persistent = b.boolean()
        parent = b.i32()
        mask = b.i32()
        events_out = []
        for mainid in range(b.u32() + 1):
            while True:
                first = b.i32()
                if first == -1:
                    break
                for code in read_actions(b):
                    events_out.append('// ===== %s (%d) =====\n%s' % (
                        MAIN_EVENTS.get(mainid, 'Main%d' % mainid), first, code))
        if events_out:
            with open(os.path.join(obj_dir, name + '.gml'), 'w',
                      encoding='utf-8') as f:
                f.write('\n\n'.join(events_out))
        objects.append({'id': i, 'name': name, 'sprite': sprite_id,
                        'solid': solid, 'visible': visible, 'depth': depth,
                        'persistent': persistent, 'parent': parent,
                        'mask': mask, 'events': len(events_out)})
    summary['objects'] = objects

    # ---- rooms ----
    room_dir = mkdir('rooms')
    assert r.u32() == 800
    rooms = []
    for i in range(r.u32()):
        b = r.inflate()
        if not b.boolean():
            rooms.append(None)
            continue
        name = b.string()
        b.skip(8)
        rver = b.u32()
        assert rver == 541, 'room version %d' % rver
        caption = b.string()
        w, h = b.u32(), b.u32()
        b.u32(); b.u32()             # snap
        b.boolean()                  # isometric
        speed = b.u32()
        b.boolean()                  # persistent
        bgcol = b.u32()
        b.u32()                      # draw bg colour / views clear
        creation = b.string()
        for _ in range(b.u32()):     # background defs
            b.boolean(); b.boolean(); b.i32()
            b.i32(); b.i32()
            b.boolean(); b.boolean()
            b.i32(); b.i32()
            b.boolean()
        b.boolean()                  # views enabled
        for _ in range(b.u32()):     # views
            b.boolean()
            [b.i32() for _ in range(6)]
            b.i32(); b.i32()
            [b.i32() for _ in range(4)]
            b.i32()
        instances = []
        for _ in range(b.u32()):
            x, y = b.i32(), b.i32()
            oid = b.i32()
            iid = b.i32()
            cc = b.string()
            b.boolean()              # locked
            instances.append({'x': x, 'y': y, 'obj': oid, 'id': iid,
                              'code': cc})
        tiles = []
        for _ in range(b.u32()):
            tx, ty = b.i32(), b.i32()
            bg = b.i32()
            bx, by = b.i32(), b.i32()
            tw, th = b.i32(), b.i32()
            depth = b.i32()
            b.i32()                  # tile id
            b.boolean()              # locked
            tiles.append({'x': tx, 'y': ty, 'bg': bg, 'bx': bx, 'by': by,
                          'w': tw, 'h': th, 'depth': depth})
        room = {'id': i, 'name': name, 'caption': caption, 'w': w, 'h': h,
                'speed': speed, 'bgcolor': bgcol, 'instances': instances,
                'tiles': tiles, 'creation_code': creation}
        with open(os.path.join(room_dir, name + '.json'), 'w',
                  encoding='utf-8') as f:
            json.dump(room, f, indent=1)
        if creation.strip():
            with open(os.path.join(room_dir, name + '_create.gml'), 'w',
                      encoding='utf-8') as f:
                f.write(creation)
        rooms.append({'id': i, 'name': name, 'w': w, 'h': h, 'speed': speed,
                      'instances': len(instances), 'tiles': len(tiles)})
        # trailing editor settings are ignored -- the blob ends here for us
    summary['rooms'] = rooms

    with open(os.path.join(outdir, 'summary.json'), 'w', encoding='utf-8') as f:
        json.dump(summary, f, indent=1)

    print('sounds      %d' % len([s for s in sounds if s]))
    print('sprites     %d' % len([s for s in sprites if s]))
    print('backgrounds %d' % len([s for s in backgrounds if s]))
    print('scripts     %d' % len([s for s in scripts if s]))
    print('objects     %d' % len([s for s in objects if s]))
    print('rooms       %d' % len([s for s in rooms if s]))


if __name__ == '__main__':
    main()
