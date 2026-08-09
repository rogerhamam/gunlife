"""Cut the tank's own geometry into a hull and a turret.

The Meshy export is one mesh, so the turret cannot traverse independently of
the tracks. Rather than model a replacement turret, this splits the triangles
that are already there: everything above the hull deck goes into the turret,
everything below into the hull, and the turret is re-centred on its ring so it
rotates about the right axis instead of swinging around the model origin.

The deck height is found from the model rather than guessed. Sweeping a
horizontal plane up through the tank, the cross-section is wide and roughly
constant through the hull, then drops sharply where the turret starts -- so
the split goes at the biggest drop in the upper half of the model.

Writes assets/models/tank_hull.obj and tank_turret.obj (plus .mtl), both
referencing the albedo already extracted from the GLB, and prints the turret
pivot for src/vehicle.cpp.

  python tools/split_tank.py [--at 0.52]
"""
import json
import os
import struct
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS = os.path.join(ROOT, "assets", "models")
SRC = os.path.join(MODELS, "tank.glb")
LOWER_NAME, UPPER_NAME = "tank_hull", "tank_turret"
ALBEDO = "tank_albedo.png"

CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942

# glTF accessor component types -> numpy dtype
CTYPE = {5120: "<i1", 5121: "<u1", 5122: "<i2", 5123: "<u2",
         5125: "<u4", 5126: "<f4"}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    _, _, total = struct.unpack("<III", data[:12])
    js, binary = None, b""
    off = 12
    while off < total:
        clen, ctype = struct.unpack("<II", data[off:off + 8])
        chunk = data[off + 8:off + 8 + clen]
        if ctype == CHUNK_JSON:
            js = json.loads(chunk)
        elif ctype == CHUNK_BIN:
            binary = chunk
        off += 8 + clen
    return js, binary


def accessor(js, binary, index):
    acc = js["accessors"][index]
    n = acc["count"]
    ncomp = NCOMP[acc["type"]]
    dt = np.dtype(CTYPE[acc["componentType"]])
    bv = js["bufferViews"][acc["bufferView"]]
    start = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = bv.get("byteStride") or (dt.itemsize * ncomp)
    if stride == dt.itemsize * ncomp:
        raw = binary[start:start + n * stride]
        return np.frombuffer(raw, dtype=dt).reshape(n, ncomp)
    # Interleaved: pull each element out at its stride.
    out = np.empty((n, ncomp), dtype=dt)
    for i in range(n):
        o = start + i * stride
        out[i] = np.frombuffer(binary[o:o + dt.itemsize * ncomp], dtype=dt)
    return out


def node_transform(js, node):
    """World matrix for a node, honouring matrix or TRS."""
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    if "scale" in node:
        m = np.diag(list(node["scale"]) + [1.0]) @ m
    if "rotation" in node:
        x, y, z, w = node["rotation"]
        r = np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0],
            [0, 0, 0, 1]], dtype=np.float64)
        m = r @ m
    if "translation" in node:
        t = np.eye(4)
        t[:3, 3] = node["translation"]
        m = t @ m
    return m


def gather(js, binary):
    """Every primitive in the scene, baked into world space."""
    verts, uvs, tris = [], [], []
    def walk(idx, parent):
        node = js["nodes"][idx]
        world = parent @ node_transform(js, node)
        if "mesh" in node:
            for prim in js["meshes"][node["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4:
                    continue
                p = accessor(js, binary, prim["attributes"]["POSITION"]).astype(np.float64)
                p = np.hstack([p, np.ones((len(p), 1))]) @ world.T
                base = sum(len(v) for v in verts)
                verts.append(p[:, :3])
                if "TEXCOORD_0" in prim["attributes"]:
                    uvs.append(accessor(js, binary,
                                        prim["attributes"]["TEXCOORD_0"]).astype(np.float64))
                else:
                    uvs.append(np.zeros((len(p), 2)))
                idxs = accessor(js, binary, prim["indices"]).astype(np.int64).ravel()
                tris.append(idxs.reshape(-1, 3) + base)
        for c in node.get("children", []):
            walk(c, world)
    scene = js.get("scene", 0)
    for r in js["scenes"][scene]["nodes"]:
        walk(r, np.eye(4))
    return np.vstack(verts), np.vstack(uvs), np.vstack(tris)


def find_deck(v, tris):
    """Height where the cross-section narrows: the top of the hull."""
    lo, hi = v[:, 1].min(), v[:, 1].max()
    h = hi - lo
    slabs = 60
    width = np.zeros(slabs)
    for i in range(slabs):
        y0 = lo + h * i / slabs
        y1 = lo + h * (i + 1) / slabs
        sel = v[(v[:, 1] >= y0) & (v[:, 1] < y1)]
        # Cross-section measured across the tank, not along it: the barrel is
        # long but narrow, so width is what tells hull from turret.
        width[i] = (sel[:, 2].max() - sel[:, 2].min()) if len(sel) > 2 else 0.0
    # Look for the sharpest narrowing in the middle band of the model.
    lo_i, hi_i = int(slabs * 0.25), int(slabs * 0.80)
    drops = width[lo_i:hi_i] - width[lo_i + 1:hi_i + 1]
    if len(drops) == 0:
        return lo + h * 0.5, width
    return lo + h * (lo_i + int(np.argmax(drops)) + 1) / slabs, width


def write_obj(path, name, v, uv, tris, albedo):
    mtl = os.path.splitext(path)[0] + ".mtl"
    with open(mtl, "w", encoding="utf-8") as f:
        f.write(f"newmtl {name}\nKd 1 1 1\nmap_Kd {albedo}\n")
    used = np.unique(tris)
    remap = {int(o): i + 1 for i, o in enumerate(used)}
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"# {name}: cut out of tank.glb by tools/split_tank.py\n")
        f.write(f"mtllib {os.path.basename(mtl)}\n")
        for o in used:
            x, y, z = v[o]
            f.write(f"v {x:.5f} {y:.5f} {z:.5f}\n")
        for o in used:
            u, w = uv[o]
            f.write(f"vt {u:.6f} {1.0 - w:.6f}\n")     # OBJ V runs the other way
        f.write(f"usemtl {name}\n")
        for a, b, c in tris:
            ia, ib, ic = remap[int(a)], remap[int(b)], remap[int(c)]
            f.write(f"f {ia}/{ia} {ib}/{ib} {ic}/{ic}\n")


def main():
    at = None
    if "--at" in sys.argv:
        at = float(sys.argv[sys.argv.index("--at") + 1])
    # --model lets the same cut be made on any body: the helicopter's rotor
    # comes off the same way the tank's turret does.
    global SRC, LOWER_NAME, UPPER_NAME, ALBEDO
    if "--model" in sys.argv:
        stem = sys.argv[sys.argv.index("--model") + 1]
        SRC = os.path.join(MODELS, stem + ".glb")
        LOWER_NAME, UPPER_NAME = stem + "_body", stem + "_rotor"
        ALBEDO = stem + "_albedo.png"
    if not os.path.exists(SRC):
        sys.exit(f"missing {SRC}")
    js, binary = read_glb(SRC)
    v, uv, tris = gather(js, binary)
    lo, hi = v[:, 1].min(), v[:, 1].max()

    if at is None:
        deck, width = find_deck(v, tris)
    else:
        deck = lo + (hi - lo) * at
        _, width = find_deck(v, tris)
    print(f"model height {lo:.3f} .. {hi:.3f}")
    print(f"deck at y={deck:.4f}  ({(deck - lo) / (hi - lo) * 100:.1f}% of height)")

    # A triangle belongs to the turret if its centre is above the deck.
    centres = v[tris].mean(axis=1)
    up = centres[:, 1] > deck
    print(f"{up.sum()} turret triangles, {(~up).sum()} hull triangles")
    if up.sum() < 12 or (~up).sum() < 12:
        sys.exit("split produced a degenerate half -- pass --at to place it by hand")

    # Turret pivot: the centre of the turret's own footprint, ignoring the
    # barrel. The barrel is a long thin run forward, so trimming to the middle
    # of the turret's X range keeps it from dragging the pivot out of the ring.
    tv = v[np.unique(tris[up])]
    x0, x1 = tv[:, 0].min(), tv[:, 0].max()
    span = x1 - x0
    core = tv[(tv[:, 0] > x0 + span * 0.18) & (tv[:, 0] < x0 + span * 0.72)]
    if len(core) < 8:
        core = tv
    pivot = np.array([core[:, 0].mean(), deck, core[:, 2].mean()])
    print(f"turret pivot (model space): {pivot[0]:.5f} {pivot[1]:.5f} {pivot[2]:.5f}")

    albedo = ALBEDO
    write_obj(os.path.join(MODELS, LOWER_NAME + ".obj"), LOWER_NAME,
              v, uv, tris[~up], albedo)
    # The turret is written already centred on its pivot, so the game can spin
    # it about the origin without needing to know where the ring was.
    vt = v.copy()
    vt[:, 0] -= pivot[0]
    vt[:, 2] -= pivot[2]
    write_obj(os.path.join(MODELS, UPPER_NAME + ".obj"), UPPER_NAME,
              vt, uv, tris[up], albedo)

    # And the offset the hull needs so the turret sits back where it belongs.
    print(f"\nIn src/vehicle.cpp use turret offset ({pivot[0]:.5f}, 0, "
          f"{pivot[2]:.5f}) in model units.")
    print("wrote tank_hull.obj and tank_turret.obj")


if __name__ == "__main__":
    main()
