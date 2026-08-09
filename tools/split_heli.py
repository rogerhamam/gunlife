"""Cut the gunship's main rotor off its airframe, without leaving a hole.

`split_tank.py --model heli` used to do this, and it got it badly wrong. Its
deck finder looks for the height where the model's cross-section *narrows*,
which is exactly right for a tank -- hull, then a much smaller turret -- and
exactly wrong for a helicopter, whose widest slice by far is the rotor disc
right at the top. It settled on 40% of the model height, so the cut ran
through the middle of the cabin and everything above it -- canopy, engine
deck, tail boom, fin, blades -- was welded into one piece that span on the
mast, and both halves were left open along the cut.

This does it properly:

  * The rotor disc is found as the height band with the largest horizontal
    span, which is what a rotor *is*.
  * The cut goes at the waist below the hub -- the narrowest cross-section
    between the top of the cabin and the hub, i.e. the mast.
  * The rotor is then the connected component above that cut which contains
    the disc. The tail fin also pokes above the cut, and is not connected to
    the hub up there, so it correctly stays with the airframe. A plain
    "everything above the plane" test cannot tell the two apart.
  * Triangles that *straddle* the cut and touch the rotor are written into
    **both** files. They are the sleeve of the mast, so the airframe keeps a
    capped mast and the rotor keeps a capped hub, and there is no gap in
    either -- the duplicated sleeve is a section of a round mast, so nobody
    can tell it is turning.

Writes assets/models/heli_body.obj and heli_rotor.obj (plus .mtl) and prints
the mast pivot for src/assets.cpp.

  python tools/split_heli.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from split_tank import read_glb, gather, write_obj   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS = os.path.join(ROOT, "assets", "models")
SRC = os.path.join(MODELS, "heli.glb")
ALBEDO = "heli_albedo.png"


def weld(v):
    """Vertex indices merged by position, so triangles that share an edge
    share an index even when the exporter split them for UVs."""
    _, inv = np.unique(np.round(v, 6), axis=0, return_inverse=True)
    return inv.ravel()


class Union:
    def __init__(self, n):
        self.p = np.arange(n)

    def find(self, a):
        p = self.p
        while p[a] != a:
            p[a] = p[p[a]]
            a = p[a]
        return a

    def join(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb


def disc_band(v, tris, slabs=80):
    """The height band holding the widest horizontal span: the rotor disc."""
    c = v[tris].mean(axis=1)
    lo, hi = v[:, 1].min(), v[:, 1].max()
    best, best_y = -1.0, None
    for i in range(slabs):
        y0 = lo + (hi - lo) * i / slabs
        y1 = lo + (hi - lo) * (i + 1) / slabs
        sel = c[(c[:, 1] >= y0) & (c[:, 1] < y1)]
        if len(sel) < 8:
            continue
        span = max(sel[:, 0].max() - sel[:, 0].min(),
                   sel[:, 2].max() - sel[:, 2].min())
        if span > best:
            best, best_y = span, (y0 + y1) * 0.5
    return best_y, best


def mast_cut(v, tris, disc_y, slabs=80):
    """The waist below the disc: the narrowest slice between the top of the
    cabin and the hub, which is the bare mast."""
    c = v[tris].mean(axis=1)
    lo, hi = v[:, 1].min(), v[:, 1].max()
    step = (hi - lo) / slabs
    # Search the band from halfway up the model to just under the disc.
    y = disc_y - step
    best, best_y = 1e9, disc_y - step
    while y > lo + (hi - lo) * 0.45:
        sel = c[(c[:, 1] >= y - step) & (c[:, 1] < y)]
        if len(sel) >= 3:
            span = max(sel[:, 0].max() - sel[:, 0].min(),
                       sel[:, 2].max() - sel[:, 2].min())
            if span < best:
                best, best_y = span, y - step * 0.5
        y -= step
    return best_y, best


def main():
    if not os.path.exists(SRC):
        sys.exit(f"missing {SRC}")
    js, binary = read_glb(SRC)
    v, uv, tris = gather(js, binary)
    lo, hi = v[:, 1].min(), v[:, 1].max()
    print(f"model height {lo:.4f} .. {hi:.4f}  ({len(tris)} triangles)")

    disc_y, disc_span = disc_band(v, tris)
    print(f"rotor disc at y={disc_y:.4f}, span {disc_span:.3f}")
    cut, waist = mast_cut(v, tris, disc_y)
    print(f"mast waist at y={cut:.4f}, span {waist:.3f} "
          f"({(cut - lo) / (hi - lo) * 100:.1f}% of height)")

    w = weld(v)
    tw = w[tris]
    ymin = v[tris][:, :, 1].min(axis=1)
    ymax = v[tris][:, :, 1].max(axis=1)
    above = ymin > cut
    straddling = (ymin <= cut) & (ymax > cut)

    # Connected components of the geometry that sits entirely above the cut.
    uf = Union(w.max() + 1)
    for a, b, c in tw[above]:
        uf.join(a, b)
        uf.join(b, c)
    roots = np.array([uf.find(i) for i in tw[above][:, 0]]) if above.any() \
        else np.array([], dtype=int)
    if len(roots) == 0:
        sys.exit("nothing above the cut -- the model is not what we think")

    # The rotor is whichever of those components actually spans the disc.
    ids = np.unique(roots)
    best_id, best_span = None, -1.0
    for cid in ids:
        pts = v[np.unique(tris[above][roots == cid])]
        span = max(pts[:, 0].max() - pts[:, 0].min(),
                   pts[:, 2].max() - pts[:, 2].min())
        print(f"  component above cut: {int((roots == cid).sum()):5d} tris, "
              f"span {span:.3f}")
        if span > best_span:
            best_id, best_span = cid, span

    rotor = np.zeros(len(tris), dtype=bool)
    idx_above = np.flatnonzero(above)
    rotor[idx_above[roots == best_id]] = True

    # A straddling triangle belongs to the mast sleeve if it shares a welded
    # vertex with the rotor. Those go into *both* halves so neither is open.
    rotor_verts = set(int(x) for x in np.unique(tw[rotor]))
    sleeve = np.zeros(len(tris), dtype=bool)
    for i in np.flatnonzero(straddling):
        if rotor_verts.intersection(int(x) for x in tw[i]):
            sleeve[i] = True
    print(f"{rotor.sum()} rotor triangles, {(~rotor).sum()} airframe "
          f"triangles, {sleeve.sum()} shared mast-sleeve triangles")
    if rotor.sum() < 12:
        sys.exit("degenerate split")

    # The mast axis: the middle of the sleeve's footprint, which is the ring
    # the blades actually turn about.
    sv = v[np.unique(tris[sleeve])] if sleeve.any() else v[np.unique(tris[rotor])]
    pivot = np.array([(sv[:, 0].min() + sv[:, 0].max()) * 0.5, cut,
                      (sv[:, 2].min() + sv[:, 2].max()) * 0.5])
    print(f"mast pivot (model space): {pivot[0]:.5f} {pivot[1]:.5f} "
          f"{pivot[2]:.5f}")

    write_obj(os.path.join(MODELS, "heli_body.obj"), "heli_body",
              v, uv, tris[~rotor | sleeve], ALBEDO)
    # The rotor is written already centred on the mast, so the game spins it
    # about the origin without needing to know where the ring was.
    vr = v.copy()
    vr[:, 0] -= pivot[0]
    vr[:, 2] -= pivot[2]
    write_obj(os.path.join(MODELS, "heli_rotor.obj"), "heli_rotor",
              vr, uv, tris[rotor | sleeve], ALBEDO)

    print(f"\nIn src/assets.cpp use heliPivot_ = Vector3{{{pivot[0]:.5f}f, "
          f"0.0f, {pivot[2]:.5f}f}};")
    print("wrote heli_body.obj and heli_rotor.obj")


if __name__ == "__main__":
    main()
