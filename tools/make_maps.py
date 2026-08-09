"""Generate the three shipped maps.

  devtest  the vehicle and weapon test range -- the only map with every
           vehicle type on it, tanks and gunships included
  urban    a full city: the old urban complex, the suburban streets and the
           enterable tower melded into one map, with a handful of cars
  gtj      Grand Theft Jack 3D's own map (rm_map1), converted 1:1 from the
           room data using the objects' own dimensions

Everything the GMK converter needs comes out of the extracted objects:

  obj_wall1_hor   32 long, 40 high, 8 thick, running +x from its origin
  obj_wall1_vert  the same running +y
  building        32 x 32 footprint, 128 high
  obj_pillar      16 wide, 96 high
  obj_bench       32 wide, 8 high

GTJ's (x, y) maps to our (x, z); its z (height) is our y.

  python tools/make_maps.py
"""
import json
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPS = os.path.join(ROOT, "assets", "maps")
EXTRACT = os.path.join(ROOT, "extracted")


def write(name, lines):
    path = os.path.join(MAPS, name + ".map")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {name}.map ({len(lines)} lines)")


class Plot:
    """The ground each map has already built on.

    Spawns and trees both need to land on clear ground, and getting that wrong
    is how the Urban Complex ended up starting players sealed inside a brick
    block. The game now walks a bad spawn out to open ground at load time, but
    the map should not be handing it bad ones in the first place -- so every
    footprint goes in here as it is emitted and candidates are tested against
    it before they are written.
    """

    def __init__(self):
        self.rects = []       # (x0, z0, x1, z1)

    def take(self, x0, z0, x1, z1, pad=0.0):
        self.rects.append((min(x0, x1) - pad, min(z0, z1) - pad,
                           max(x0, x1) + pad, max(z0, z1) + pad))

    def box(self, x, z, sx, sz, pad=0.0):
        self.take(x, z, x + sx, z + sz, pad)

    def clear(self, x, z, r):
        """True when a disc of radius r at (x, z) touches nothing taken."""
        for x0, z0, x1, z1 in self.rects:
            if x + r > x0 and x - r < x1 and z + r > z0 and z - r < z1:
                return False
        return True

    def scatter(self, rng, n, bounds, r, tries=400):
        """n points inside `bounds` clear of everything taken and of each
        other. Returns fewer than n rather than forcing one into a wall."""
        bx0, bz0, bx1, bz1 = bounds
        out = []
        for _ in range(tries):
            if len(out) >= n:
                break
            x = rng.randrange(int(bx0), int(bx1), 10)
            z = rng.randrange(int(bz0), int(bz1), 10)
            if not self.clear(x, z, r):
                continue
            if any((x - px) ** 2 + (z - pz) ** 2 < (r * 2.1) ** 2
                   for px, pz in out):
                continue
            out.append((x, z))
        return out


# ---------------------------------------------------------------- dev range
def make_devtest():
    L = []
    w = L.append
    w("# ============================================================================")
    w("#  Dev Test Range -- the only map carrying every vehicle in the game.")
    w("#  Flat and open on purpose: handling, ballistics and effects are all easy")
    w("#  to read with nothing in the way. Single player starts in god mode here.")
    w("#")
    w("#  car   <x> <z> <y> <turn 0-3> <kind 0 saloon 1 van 3 supercar> <r> <g> <b>")
    w("#  tank  <x> <z> <y> <turn 0-3> <r> <g> <b>")
    w("#  heli  <x> <z> <y> <turn 0-3> <r> <g> <b>")
    w("#  E to get in, WASD to drive or fly, SPACE or E to get out.")
    w("# ============================================================================")
    w("")
    w("name Dev Test Range")
    w("size 3400 3400")
    w("ground floor 128")
    w("fog 40 56 66 900 3600")
    w("sky 30 40 50")
    w("")
    w("# --------------------------------------------------------------- perimeter")
    for a in ((0, 0, 3400, 0), (0, 3400, 3400, 3400),
              (0, 0, 0, 3400), (3400, 0, 3400, 3400)):
        w(f"wall {a[0]} {a[1]} {a[2]} {a[3]} 0 300 80 concrete 64")
    w("")
    w("# ------------------------------------------------------------ vehicle line")
    w("box 520 0 1080 240 2 900 stone2 64")
    w("box 520 0 2040 300 2 620 stone2 64")
    line = [
        ("car", 620, 1140, 0, 0, (168, 172, 178)),
        ("car", 620, 1280, 0, 0, (150, 34, 30)),
        ("car", 620, 1420, 0, 0, (28, 52, 120)),
        ("car", 620, 1560, 1, 0, (40, 44, 40)),
        ("car", 620, 1700, 3, 0, (235, 232, 226)),
        ("car", 620, 1840, 3, 0, (236, 190, 40)),
    ]
    for kind, x, z, sub, _t, c in line:
        w(f"car {x} {z} 0 0 {sub} {c[0]} {c[1]} {c[2]}")
    w("tank 660 2120 0 0 92 98 74")
    w("tank 660 2300 0 0 70 76 58")
    w("# Gunships, on their own pads clear of everything else.")
    w("heli 700 2560 0 0 84 90 96")
    w("heli 700 2860 0 0 64 70 78")
    w("box 600 0 2460 200 3 200 metal 64")
    w("box 600 0 2760 200 3 200 metal 64")
    w("")
    w("# -------------------------------------------------------------- the circuit")
    w("box 1100 0 900 1600 8 24 stone 48")
    w("box 1100 0 2300 1600 8 24 stone 48")
    w("box 1100 0 900 24 8 1424 stone 48")
    w("box 2676 0 900 24 8 1424 stone 48")
    w("box 1500 0 1200 30 26 260 crate2 40")
    w("box 1900 0 1900 30 26 260 crate2 40")
    w("box 2300 0 1200 30 26 260 crate2 40")
    w("stairs 1250 1520 200 160 0 60 0 10 metal")
    w("box 1450 60 1520 90 6 160 metal 48")
    w("")
    w("# -------------------------------------------------------------- gun range")
    w("# Targets at 200, 450 and 900 units from the line, for checking hole fade,")
    w("# tracer travel and fly-by timing.")
    w("box 1660 0 2900 8 60 60 wall_b 32")
    w("box 1910 0 2900 8 60 60 wall_b 32")
    w("box 2360 0 2900 8 60 60 wall_b 32")
    w("box 1600 0 2980 800 4 12 stone2 32")
    w("box 1580 0 2700 340 4 30 stone 32")
    w("")
    w("# ---------------------------------------------------- step-up test + tower")
    w("stairs 2900 1000 120 240 0 60 2 10 stone2")
    w("stairs 2900 1300 120 240 0 80 2 10 wall_c")
    w("building 2750 1800 300 300 0 3 46 concrete stone2")
    w("")
    w("# ------------------------------------------------------------- cover props")
    w("box 1300 0 1700 60 60 60 crate 60")
    w("box 1360 0 1700 60 60 60 crate 60")
    w("box 1300 56 1700 60 60 60 crate 60")
    w("box 2000 0 1150 90 34 140 tech 48")
    w("box 2450 0 2000 100 110 60 tech2 64")
    w("box 1100 0 2000 40 120 40 pillar 40")
    w("box 1100 0 1150 40 120 40 pillar 40")
    w("")
    w("# ------------------------------------------------------------------ spawns")
    for x, z, yaw in ((820, 1500, 180), (820, 1800, 180), (820, 2700, 180),
                      (1600, 2600, 90), (1700, 1600, 0), (2400, 1600, 180),
                      (1700, 1100, 270), (2800, 2800, 225), (1000, 2900, 315)):
        w(f"spawn {x} 4 {z} {yaw}")
    write("devtest", L)


# ------------------------------------------------------------------- urban
def make_urban():
    """The old urban complex, the suburbs and the tower, melded into one city."""
    rng = random.Random(20260810)
    L = []
    w = L.append
    plot = Plot()
    SX, SZ = 4200, 3000
    w("# ============================================================================")
    w("#  Urban Complex -- a downtown block, a suburban quarter of houses and")
    w("#  gardens, and the enterable tower, joined into one city by its streets.")
    w("#  Only a few cars: this is a map to fight through, not to drive around.")
    w("#  Generated by tools/make_maps.py.")
    w("# ============================================================================")
    w("")
    w("name Urban Complex")
    w(f"size {SX} {SZ}")
    w("ground floor 64")
    w("fog 30 46 54 900 3600")
    w("sky 26 34 42")
    w("")
    w("# --------------------------------------------------------------- perimeter")
    for a in ((0, 0, SX, 0), (0, SZ, SX, SZ), (0, 0, 0, SZ), (SX, 0, SX, SZ)):
        w(f"wall {a[0]} {a[1]} {a[2]} {a[3]} 0 240 70 brick 64")
    # Keep everything off the perimeter wall itself.
    plot.take(-100, -100, SX + 100, 60)
    plot.take(-100, SZ - 60, SX + 100, SZ + 100)
    plot.take(-100, -100, 60, SZ + 100)
    plot.take(SX - 60, -100, SX + 100, SZ + 100)
    w("")

    w("# ================================================== downtown (west third)")
    w("# Tall blocks on a grid, alleys between them, crate cover in the gaps.")
    texes = ["brick", "wall_a", "wall_b", "wall_d", "concrete", "stone"]
    for gx in range(3):
        for gz in range(4):
            x = 160 + gx * 440
            z = 160 + gz * 700
            h = rng.choice((120, 150, 180, 210))
            w(f"box {x} 0 {z} 330 {h} 520 {rng.choice(texes)} 64")
            plot.box(x, z, 330, 520)
    w("box 140 0 2760 300 92 150 bank 128")
    plot.box(140, 2760, 300, 150)
    w("stairs 500 620 80 320 0 100 3 20 metal")
    plot.box(500, 620, 80, 320)
    w("box 500 90 560 90 10 70 metal 16")
    for x, z in plot.scatter(rng, 8, (180, 160, 1400, 2800), 46):
        w(f"box {x} 0 {z} 56 56 56 {rng.choice(('crate', 'crate2'))} 56")
        plot.box(x, z, 56, 56)
    w("")

    w("# ================================================== the tower (centre)")
    w("building 1720 1240 340 340 0 5 46 concrete stone2")
    plot.box(1720, 1240, 340, 340)
    w("box 1620 0 900 60 60 60 crate2 60")
    w("box 1620 56 900 60 60 60 crate2 60")
    plot.box(1620, 900, 60, 60)
    w("box 2100 0 1000 40 30 200 stone 48")
    plot.box(2100, 1000, 40, 200)
    w("box 1700 0 1680 220 26 50 stone2 48")
    w("# crate ladder up the side of the tower")
    for i in range(5):
        w(f"box 1660 0 {1620 - i * 44} 44 {26 + i * 26} 44 crate 44")
    plot.box(1660, 1400, 44, 264)
    w("")

    w("# ============================== the new quarter (centre, north + south)")
    w("# The ground between downtown and the suburbs used to be bare. These are")
    w("# `building` and `townhouse` lines, so every one of them is enterable:")
    w("# doors, windows, floor slabs and stairs all the way to the roof.")
    w("# North: office blocks on a grid.")
    for i, (bx, bz, storeys) in enumerate(((1440, 140, 4), (1830, 140, 6),
                                           (2200, 140, 3), (1440, 620, 5),
                                           (1830, 620, 3), (2200, 620, 4))):
        wt = ("concrete", "stone2", "wall_b", "brick")[i % 4]
        w(f"building {bx} {bz} 300 300 0 {storeys} 46 {wt} stone2")
        plot.box(bx - 20, bz - 20, 340, 340)
    w("# ...and a low civic block closing the north end")
    w("building 1440 1000 700 240 0 2 46 wall_d floor")
    plot.box(1420, 980, 740, 280)
    w("")
    w("# South: a street of town houses with pitched roofs, two storeys each.")
    w("# townhouse <x> <z> <sx> <sz> <y> <storeys> <storeyH> <wall> <roof> <floor>")
    for row, bz in enumerate((1760, 2160, 2560)):
        for col in range(4):
            bx = 1440 + col * 230
            wt = ("brick", "wall_c", "wall_a", "brick")[(row + col) % 4]
            w(f"townhouse {bx} {bz} 190 170 0 2 40 {wt} wood floor")
            plot.box(bx - 14, bz - 14, 218, 198)
    w("# the lane serving them")
    for bz in (1700, 2100, 2500):
        w(f"box 1400 0 {bz} 940 3 44 stone2 96")
    w("")

    w("# ================================================== suburbs (east half)")
    w("# Detached houses on plots either side of two residential streets, with")
    w("# gardens, fences and driveways between them.")
    for row in range(3):
        for col in range(5):
            x = 2400 + col * 350
            z = 260 + row * 900
            w(f"box {x} 0 {z} 220 {rng.choice((72, 84, 96))} 200 "
              f"{rng.choice(('wall_c', 'wall_a', 'brick'))} 48")
            # porch roof and a garden wall
            w(f"box {x - 20} 0 {z + 200} 260 8 24 wood 32")
            w(f"wall {x - 30} {z + 250} {x + 250} {z + 250} 0 34 10 wood 32")
            plot.box(x - 30, z, 290, 260)
    w("# residential streets")
    for z in (480, 1380, 2280):
        w(f"box 2300 0 {z} {SX - 2400} 3 90 stone2 96")
    w("# a park at the far corner")
    w("box 3600 0 2500 500 3 400 floor 96")
    w("")

    w("# ================================================= the avenue joining them")
    w(f"box 1400 0 1420 1000 3 200 stone2 96")
    w("box 1900 0 1400 40 30 40 pillar 40")
    w("box 2200 0 1600 40 30 40 pillar 40")
    w("box 1500 0 1340 120 34 40 wood 32")
    w("box 2050 0 1620 120 34 40 wood 32")
    plot.box(1880, 1380, 80, 80)
    plot.box(2180, 1580, 80, 80)
    plot.box(1490, 1330, 140, 60)
    plot.box(2040, 1610, 140, 60)
    w("")

    w("# ------------------------------------------------------------------- cars")
    w("# Only a handful, parked. The vehicle test range is the map for driving.")
    for cx, cz, turn, kind, col in ((1520, 1500, 0, 0, (150, 34, 30)),
                                    (2260, 520, 0, 0, (28, 52, 120)),
                                    (2960, 1420, 0, 1, (40, 44, 40)),
                                    (3620, 2320, 2, 0, (196, 190, 178)),
                                    (900, 2760, 2, 3, (235, 232, 226))):
        w(f"car {cx} {cz} 0 {turn} {kind} {col[0]} {col[1]} {col[2]}")
        plot.box(cx - 50, cz - 50, 100, 100)
    w("")

    # Spawn points are chosen *before* the trees are planted, so that a tree
    # can never take a spawn's ground -- the trees have a whole city to grow
    # in and the spawns do not.
    # Streets and alleys, not the middle of a plot: downtown's grid leaves
    # gaps at x = 490..600 / 930..1040 and z = 680..860 / 1380..1560 /
    # 2080..2260, and the suburbs leave one between every pair of houses.
    wanted = ((545, 770, 0), (985, 1470, 180), (545, 2170, 90),
              (985, 2880, 270), (1500, 1520, 0), (1880, 1000, 180),
              (2680, 620, 270), (3030, 1520, 90), (3380, 2420, 180),
              (3730, 620, 135), (2680, 2420, 45), (4110, 1500, 180),
              (300, 1470, 0), (2180, 2600, 315))
    spawnLines = []
    for x, z, yaw in wanted:
        if not plot.clear(x, z, 26):
            print(f"  urban: spawn ({x}, {z}) is not clear, dropped")
            continue
        spawnLines.append(f"spawn {x} 4 {z} {yaw}")
        plot.box(x - 60, z - 60, 120, 120)

    # ------------------------------------------------------------------ trees
    # Kingdom's bark and leaves, on this game's own brush-built trees. The
    # park is a proper stand of them; the rest line the streets and fill the
    # gardens, and none of them is allowed to land on anything already built.
    w("# ------------------------------------------------------------------- trees")
    w("# Bark and leaf textures carried over from Kingdom (DARK CROWN) by")
    w("# tools/import_kingdom_trees.py. tree <x> <z> <y> <height> [seed]")
    seed = 1
    planted = []

    def plant(x, z, height, r=None):
        nonlocal seed
        w(f"tree {x} {z} 0 {height} {seed}")
        # A tree's crown reaches about 0.3 * height either side of the trunk.
        plot.box(x - 12, z - 12, 24, 24)
        planted.append((x, z))
        seed += 1

    w("# the park")
    for x, z in plot.scatter(rng, 14, (3630, 2530, 4070, 2870), 62):
        plant(x, z, rng.randrange(110, 190, 10))
    w("# gardens and verges along the residential streets")
    for z in (480, 1380, 2280):
        for x in range(2680, 4060, 340):
            if plot.clear(x, z + 130, 56):
                plant(x, z + 130, rng.randrange(90, 150, 10))
            if plot.clear(x + 170, z - 60, 56):
                plant(x + 170, z - 60, rng.randrange(90, 150, 10))
    w("# the avenue, and a few in the downtown alleys")
    for x, z in plot.scatter(rng, 6, (1420, 1300, 2360, 1660), 60):
        plant(x, z, rng.randrange(100, 160, 10))
    for x, z in plot.scatter(rng, 10, (120, 120, 1400, 2880), 60):
        plant(x, z, rng.randrange(90, 170, 10))
    print(f"  urban: {len(planted)} trees")
    w("")

    w("# ----------------------------------------------------------------- spawns")
    w("# Every one of these was tested against the geometry above before it was")
    w("# written -- the old hand-typed set had four of them inside blocks.")
    L.extend(spawnLines)
    print(f"  urban: {len(spawnLines)} spawns placed on clear ground")
    write("urban", L)


# --------------------------------------------------------------------- GTJ
def make_gtj():
    """Convert GTJ3D's own rm_map1 room, 1:1, using the objects' dimensions."""
    summary = json.load(open(os.path.join(EXTRACT, "summary.json"),
                             encoding="utf-8"))
    names = {o["id"]: o["name"] for o in summary["objects"] if o}
    room = json.load(open(os.path.join(EXTRACT, "rooms", "rm_map1.json"),
                          encoding="utf-8"))
    RW, RH = room["w"], room["h"]

    L = []
    w = L.append
    w("# ============================================================================")
    w("#  Grand Theft Jack 3D -- rm_map1, converted 1:1 by tools/make_maps.py.")
    w("#")
    w("#  Every instance in the original room is placed at its own coordinates,")
    w("#  with the dimensions its object declared in GTJ3D's create event:")
    w("#    obj_wall1_hor / _vert  32 long, 40 high, 8 thick")
    w("#    building               32 x 32 footprint, 128 high")
    w("#    obj_pillar             16 wide, 96 high")
    w("#    obj_bench / _metal     32 wide, 8 high")
    w("#  GTJ's (x, y) is our (x, z); its z is our y.")
    w("# ============================================================================")
    w("")
    w("name Grand Theft Jack")
    w(f"size {RW} {RH}")
    w("ground floor 64")
    w("fog 0 120 132 700 2600")
    w("sky 22 30 38")
    w("")
    w("# --------------------------------------------------------------- perimeter")
    for a in ((0, 0, RW, 0), (0, RH, RW, RH), (0, 0, 0, RH), (RW, 0, RW, RH)):
        w(f"wall {a[0]} {a[1]} {a[2]} {a[3]} 0 200 24 brick 32")
    w("")

    counts = {}
    plot = Plot()
    walls, props, spawns, cars, trees = [], [], [], [], []
    treeSeed = 1
    for inst in room["instances"]:
        n = names.get(inst["obj"], "")
        x, z = inst["x"], inst["y"]
        counts[n] = counts.get(n, 0) + 1
        if n == "obj_wall1_hor":
            walls.append(f"wall {x} {z} {x + 32} {z} 0 40 8 wall_a 32")
            plot.box(x - 8, z - 8, 48, 16)
        elif n == "obj_wall1_vert":
            walls.append(f"wall {x} {z} {x} {z + 32} 0 40 8 wall_a 32")
            plot.box(x - 8, z - 8, 16, 48)
        elif n == "obj_fakedoor_hor":
            walls.append(f"wall {x} {z} {x + 32} {z} 0 40 8 door 32")
            plot.box(x - 8, z - 8, 48, 16)
        elif n == "building":
            props.append(f"box {x} 0 {z} 32 128 32 stone2 32")
            plot.box(x, z, 32, 32)
        elif n == "obj_pillar":
            props.append(f"box {x} 0 {z} 16 96 16 pillar 16")
            plot.box(x, z, 16, 16)
        elif n in ("obj_bench", "obj_metal_bench"):
            props.append(f"box {x} 0 {z} 32 8 16 "
                         f"{'metal' if 'metal' in n else 'wood'} 32")
            plot.box(x, z, 32, 16)
        elif n == "obj_vending_machine":
            props.append(f"box {x} 0 {z} 24 48 20 tech 24")
            plot.box(x, z, 24, 20)
        elif n == "obj_barrel":
            props.append(f"box {x} 0 {z} 20 28 20 metal 20")
            plot.box(x, z, 20, 20)
        elif n == "obj_plant":
            # GTJ's potted plants become real trees, on Kingdom's bark and
            # leaves. They stood in the street and the plazas, so they are
            # exactly where a city's trees belong.
            trees.append(f"tree {x + 8} {z + 8} 0 {56 + (treeSeed * 7) % 34} "
                         f"{treeSeed}")
            plot.box(x - 6, z - 6, 28, 28)
            treeSeed += 1
        elif n == "obj_sign" or n == "obj_police_station_sign" \
                or n == "obj_mcdonald_sign":
            props.append(f"box {x} 0 {z} 8 64 24 metal_panel 32")
        elif n == "obj_floor_pair":
            props.append(f"box {x} 0 {z} 64 6 64 floor 64")
            plot.box(x, z, 64, 64)
        elif n == "obj_stove":
            props.append(f"box {x} 0 {z} 28 26 24 metal_panel2 24")
            plot.box(x, z, 28, 24)
        elif n == "obj_bankvault_door":
            props.append(f"box {x} 0 {z} 12 64 48 metal_door 32")
            plot.box(x, z, 12, 48)
        elif n == "obj_car":
            cars.append((x, z))
            plot.box(x - 50, z - 50, 100, 100)
        elif n == "obj_player":
            spawns.append((x, z, 0))
        elif n in ("obj_policeman", "obj_civilian", "obj_shopkeeper",
                   "obj_swat_station", "obj_policestation", "obj_airport"):
            # People and stations become spawn points -- the places the
            # original map expected someone to be standing.
            spawns.append((x, z, 0))

    w(f"# {len(walls)} wall segments, {len(props)} props, {len(cars)} cars")
    w("")
    w("# ------------------------------------------------------------------ walls")
    L.extend(walls)
    w("")
    w("# ------------------------------------------------------------------ props")
    L.extend(props)
    w("")
    w("# ------------------------------------------------------------------ trees")
    w("# GTJ3D's obj_plant instances, grown into real trees, plus a scattering")
    w("# through the open ground between the blocks. Bark and leaf textures")
    w("# from Kingdom (DARK CROWN) via tools/import_kingdom_trees.py.")
    rng = random.Random(20260322)
    for x, z in plot.scatter(rng, 40, (120, 120, RW - 120, RH - 120), 44):
        trees.append(f"tree {x} {z} 0 {rng.randrange(60, 130, 10)} {treeSeed}")
        plot.box(x - 14, z - 14, 28, 28)
        treeSeed += 1
    L.extend(trees)
    print(f"  gtj: {len(trees)} trees")
    w("")
    w("# ------------------------------------------------------------------- cars")
    palette = [(150, 34, 30), (28, 52, 120), (196, 190, 178), (30, 30, 34),
               (150, 128, 40), (40, 104, 82)]
    for i, (x, z) in enumerate(cars):
        r, g, b = palette[i % len(palette)]
        w(f"car {x} {z} 0 {i % 4} 0 {r} {g} {b}")
    w("")
    w("# ----------------------------------------------------------------- spawns")
    if len(spawns) < 8:
        # Top up around the edges so a deathmatch has somewhere to put people.
        for i in range(8 - len(spawns)):
            spawns.append((160 + (i % 4) * (RW - 320) // 3,
                           160 + (i // 4) * (RH - 320), 0))
    seen = set()
    for x, z, yaw in spawns[:24]:
        key = (x // 64, z // 64)
        if key in seen:
            continue
        seen.add(key)
        w(f"spawn {x} 4 {z} {yaw}")

    print("  GMK objects converted:",
          ", ".join(f"{k}x{v}" for k, v in sorted(counts.items(),
                                                  key=lambda kv: -kv[1])[:8]))
    write("gtj", L)


def main():
    # Only these three ship; anything else in assets/maps is removed so the
    # menu and the map list cannot disagree.
    keep = {"devtest.map", "urban.map", "gtj.map"}
    for f in os.listdir(MAPS):
        if f.endswith(".map") and f not in keep:
            os.remove(os.path.join(MAPS, f))
            print(f"removed {f}")
    make_devtest()
    make_urban()
    make_gtj()


if __name__ == "__main__":
    main()
