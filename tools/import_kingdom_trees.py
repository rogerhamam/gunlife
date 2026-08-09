"""Bring the trees over from Kingdom (DARK CROWN).

Kingdom ships no tree meshes -- its trunks and canopies are built in code and
skinned with two tiling textures, `bark.png` and `leaves.png`. Those are the
part of the tree that is actually authored art, so those are what come across;
the geometry is rebuilt here out of brushes by World::AddTree, in this game's
own hard-edged style, rather than trying to lift a mesh that does not exist.

Both are 1024x1024 and fully opaque, which is what makes them usable on solid
brushes: no alpha means no cutout shader and no sorting, so a tree is just
world geometry like everything else -- it collides, it takes bullet holes, it
is lit by muzzle flashes and it sits in the fog with the buildings.

They are resampled to 256 to sit alongside the game's own 128-pixel textures
without being ten times their weight.

  python tools/import_kingdom_trees.py [--from <kingdom assets/textures dir>]
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, "assets", "textures")
DEFAULT_SRC = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs",
                           "Kingdom", "assets", "textures")
SIZE = 256
WANTED = ("bark", "leaves")


def main():
    src = DEFAULT_SRC
    if "--from" in sys.argv:
        src = sys.argv[sys.argv.index("--from") + 1]
    if not os.path.isdir(src):
        sys.exit(f"Kingdom textures not found at {src}\n"
                 f"pass --from <dir> pointing at its assets/textures")

    for name in WANTED:
        s = os.path.join(src, name + ".png")
        if not os.path.exists(s):
            print(f"  {name}.png missing from {src}, skipped")
            continue
        im = Image.open(s).convert("RGBA")
        before = im.size
        im = im.resize((SIZE, SIZE), Image.LANCZOS)
        out = os.path.join(DST, name + ".png")
        im.save(out, optimize=True)
        print(f"  {name}.png  {before[0]}x{before[1]} -> {SIZE}x{SIZE}  "
              f"({os.path.getsize(out) // 1024} KB)")

    print("done -- 'bark' and 'leaves' are now map textures; use the map's "
          "`tree` command to plant them.")


if __name__ == "__main__":
    main()
