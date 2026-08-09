#include "world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace kaj {
namespace {

inline bool Overlap(const Brush& b, Vector3 mn, Vector3 mx) {
  return mn.x < b.max.x && mx.x > b.min.x &&
         mn.y < b.max.y && mx.y > b.min.y &&
         mn.z < b.max.z && mx.z > b.min.z;
}

Color ParseColor(std::istringstream& in, Color fallback) {
  int r, g, b;
  if (in >> r >> g >> b) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
  }
  return fallback;
}

}  // namespace

void World::AddBox(Vector3 mn, Vector3 mx, const std::string& tex, float tile) {
  Brush b;
  b.min = Vector3{std::min(mn.x, mx.x), std::min(mn.y, mx.y), std::min(mn.z, mx.z)};
  b.max = Vector3{std::max(mn.x, mx.x), std::max(mn.y, mx.y), std::max(mn.z, mx.z)};
  b.tex = tex;
  b.tile = tile > 0.0f ? tile : 32.0f;
  brushes_.push_back(b);
}

// A GTJ-style line wall: a segment given a thickness and a height.
void World::AddWall(float x1, float z1, float x2, float z2, float y,
                    float height, float thickness, const std::string& tex,
                    float tile) {
  const float h = thickness * 0.5f;
  Vector3 mn{std::min(x1, x2) - h, y, std::min(z1, z2) - h};
  Vector3 mx{std::max(x1, x2) + h, y + height, std::max(z1, z2) + h};
  AddBox(mn, mx, tex, tile);
}

// Staircase built out of steps. dir: 0=+X, 1=-X, 2=+Z, 3=-Z is the ascent.
void World::AddStairs(float x, float z, float sx, float sz, float y,
                      float height, int dir, int steps, const std::string& tex) {
  if (steps < 1) steps = 1;
  for (int i = 0; i < steps; ++i) {
    const float f = static_cast<float>(i + 1) / steps;
    const float f0 = static_cast<float>(i) / steps;
    const float top = y + height * f;
    Vector3 mn, mx;
    if (dir == 0)      { mn = {x + sx * f0, y, z};             mx = {x + sx * f, top, z + sz}; }
    else if (dir == 1) { mn = {x + sx * (1 - f), y, z};        mx = {x + sx * (1 - f0), top, z + sz}; }
    else if (dir == 2) { mn = {x, y, z + sz * f0};             mx = {x + sx, top, z + sz * f}; }
    else               { mn = {x, y, z + sz * (1 - f)};        mx = {x + sx, top, z + sz * (1 - f0)}; }
    AddBox(mn, mx, tex, 32.0f);
  }
}

void World::AddTree(float x, float z, float y, float height, int seed) {
  if (height < 24.0f) height = 24.0f;
  // Deterministic per-tree jitter: the same map file always grows the same
  // wood, but no two trees on it are the same shape.
  uint32_t s = static_cast<uint32_t>(seed) * 2654435761u + 1013904223u;
  auto rnd = [&s]() {
    s = s * 1664525u + 1013904223u;
    return static_cast<float>((s >> 16) & 0x7fff) / 32767.0f;
  };

  // Trunk. Thin enough to hide behind, tall enough that the crown clears
  // head height and you walk under a tree rather than into it.
  const float trunkR = fmaxf(3.0f, height * 0.040f);
  const float trunkH = height * (0.52f + rnd() * 0.10f);
  AddBox(Vector3{x - trunkR, y, z - trunkR},
         Vector3{x + trunkR, y + trunkH, z + trunkR}, "bark", 44.0f);

  // A branch stub reaching out under the crown, which is most of what stops
  // the trunk reading as a fence post.
  const float bl = height * (0.10f + rnd() * 0.06f);
  const float by = y + trunkH * (0.74f + rnd() * 0.10f);
  const float bt = trunkR * 0.60f;
  if (rnd() < 0.5f) {
    AddBox(Vector3{x + trunkR - 1.0f, by, z - bt},
           Vector3{x + trunkR + bl, by + bt * 2.2f, z + bt}, "bark", 32.0f);
  } else {
    AddBox(Vector3{x - bt, by, z + trunkR - 1.0f},
           Vector3{x + bt, by + bt * 2.2f, z + trunkR + bl}, "bark", 32.0f);
  }

  // Crown: a clump of lobes scattered around the top of the trunk rather
  // than slabs stacked on it. Concentric slabs gave a stepped green ziggurat
  // -- every tier's flat top and square shoulders lined up, and a stand of
  // them read as a pile of crates. Lobes at different heights, radii and
  // sizes interlock instead, so the outline is ragged from every angle.
  const float crownR = height * 0.23f;
  const float cy = y + trunkH + crownR * 0.55f;
  const int lobes = 5;
  const float phase = rnd() * 2.0f * PI;
  for (int i = 0; i < lobes; ++i) {
    // One lobe on the axis, the rest spread around it at uneven bearings.
    const float ang = phase + (2.0f * PI * i) / (lobes - 1) + (rnd() - 0.5f);
    const float rad = (i == 0) ? 0.0f : crownR * (0.42f + rnd() * 0.44f);
    const float half = crownR * (i == 0 ? 0.70f : 0.40f + rnd() * 0.28f);
    const float lx = x + cosf(ang) * rad;
    const float lz = z + sinf(ang) * rad;
    // Lower lobes hang wider, the top one caps it off.
    const float ly = cy + (i == 0 ? crownR * 0.28f
                                  : (rnd() - 0.42f) * crownR * 0.85f);
    AddBox(Vector3{lx - half, ly - half * 0.78f, lz - half},
           Vector3{lx + half, ly + half * 0.78f, lz + half}, "leaves", 60.0f);
  }
}

// One storey's outer walls, with a centred opening per side: a doorway at
// ground level on the -X face so there is always a way in, a window with a
// sill and a header everywhere else.
void World::AddStoreyShell(float x0, float z0, float x1, float z1, float by,
                           float storeyH, float th, bool ground,
                           const std::string& wallTex, float openFrac,
                           float sill, float head) {
  // side 0 = -X (the entrance side), 1 = +X, 2 = -Z, 3 = +Z
  for (int side = 0; side < 4; ++side) {
    const bool alongZ = (side < 2);
    const float span = alongZ ? (z1 - z0) : (x1 - x0);
    const float openW = fminf(alongZ ? 118.0f : 130.0f, span * openFrac);
    const float mid = (alongZ ? (z0 + z1) : (x0 + x1)) * 0.5f;
    const float a0 = mid - openW * 0.5f, a1 = mid + openW * 0.5f;
    const float fixed = (side == 0) ? x0 : (side == 1) ? x1
                      : (side == 2) ? z0 : z1;
    const bool doorway = ground && side == 0;

    auto seg = [&](float b0, float b1, float wy, float wh) {
      if (b1 - b0 < 1.0f || wh < 1.0f) return;
      if (alongZ) AddWall(fixed, b0, fixed, b1, wy, wh, th, wallTex, 48.0f);
      else        AddWall(b0, fixed, b1, fixed, wy, wh, th, wallTex, 48.0f);
    };

    const float lo = alongZ ? z0 : x0, hi = alongZ ? z1 : x1;
    seg(lo, a0, by, storeyH);               // solid either side
    seg(a1, hi, by, storeyH);
    if (!doorway) seg(a0, a1, by, sill);    // sill under the window
    seg(a0, a1, by + storeyH - head, head); // header over the opening
  }
}

// A house you can walk into: shell, floors, a straight stair up and a pitched
// roof. Everything here is at domestic scale, which is what lets the stair be
// a single flight -- AddBuilding needs a switchback because its storeys are
// tall enough that a straight run would collide with the floor above it.
void World::AddTownhouse(float x, float z, float sx, float sz, float y,
                         int storeys, float storeyH,
                         const std::string& wallTex, const std::string& roofTex,
                         const std::string& floorTex) {
  if (storeys < 1) storeys = 1;
  const float th = 12.0f;                    // thinner walls than a tower
  const float x0 = x, x1 = x + sx;
  const float z0 = z, z1 = z + sz;
  const float sill = 12.0f, head = 10.0f;
  const float ft = 6.0f;                     // floor slab thickness

  // The stair runs up the +X end of the house, against the far wall from the
  // door, climbing toward -Z. The void it comes up through is cut out of the
  // slab above it.
  const float stairW = fminf(64.0f, sx * 0.30f);
  const float vx0 = x1 - th - stairW, vx1 = x1 - th;
  const float run = fminf(sz - th * 2.0f - 8.0f, 150.0f);
  const float vz1 = z1 - th - 4.0f, vz0 = vz1 - run;

  for (int s = 0; s < storeys; ++s) {
    const float by = y + s * storeyH;
    AddStoreyShell(x0, z0, x1, z1, by, storeyH, th, s == 0, wallTex, 0.30f,
                   sill, head);

    // Ceiling of this storey. Four slabs, leaving the stairwell open -- on
    // the top storey it is the ceiling under the roof space.
    const float fy = by + storeyH;
    const bool needVoid = (s + 1 < storeys);
    if (needVoid) {
      AddBox({x0, fy - ft, z0}, {vx0, fy, z1}, floorTex, 48.0f);
      AddBox({vx0, fy - ft, z0}, {vx1, fy, vz0}, floorTex, 48.0f);
      AddBox({vx0, fy - ft, vz1}, {vx1, fy, z1}, floorTex, 48.0f);
      AddBox({vx1, fy - ft, z0}, {x1, fy, z1}, floorTex, 48.0f);
      // One straight flight. Keep the rise per step under the 6 units the
      // player can walk up without jumping.
      const int steps = static_cast<int>(ceilf(storeyH / 5.0f));
      AddStairs(vx0 + 2.0f, vz0 + 2.0f, (vx1 - vx0) - 4.0f, run - 4.0f, by,
                storeyH, 3, steps, "wood");
    } else {
      AddBox({x0, fy - ft, z0}, {x1, fy, z1}, floorTex, 48.0f);
    }
  }

  // --- pitched roof --------------------------------------------------------
  // Stepped slabs narrowing toward the ridge, which runs along the longer
  // axis. Brushes are axis-aligned, so this is as close to a gable as the
  // geometry allows -- and it is close enough that from the street it reads
  // as a pitched roof rather than as a flat one.
  const float ry = y + storeys * storeyH;
  const bool ridgeAlongX = sx >= sz;
  const float shortSpan = ridgeAlongX ? sz : sx;
  const int steps = 5;
  const float eave = 8.0f;                   // overhang past the walls
  const float pitch = shortSpan * 0.42f;     // total roof height
  for (int i = 0; i < steps; ++i) {
    // Each slab is inset from the last, so the stack tapers to the ridge.
    const float f = static_cast<float>(i) / steps;
    const float inset = (shortSpan * 0.5f - 2.0f) * f;
    const float sy = ry + pitch * f;
    const float sh = pitch / steps + 1.0f;   // overlap, so there is no gap
    if (ridgeAlongX) {
      AddBox({x0 - eave, sy, z0 - eave + inset},
             {x1 + eave, sy + sh, z1 + eave - inset}, roofTex, 40.0f);
    } else {
      AddBox({x0 - eave + inset, sy, z0 - eave},
             {x1 + eave - inset, sy + sh, z1 + eave}, roofTex, 40.0f);
    }
  }
}

void World::AddBuilding(float x, float z, float sx, float sz, float y,
                        int storeys, float storeyH, const std::string& wallTex,
                        const std::string& floorTex) {
  if (storeys < 1) storeys = 1;
  const float th = 18.0f;                     // wall thickness
  const float x0 = x, x1 = x + sx;
  const float z0 = z, z1 = z + sz;

  // Stairwell void, tucked into the -X/+Z corner. Floor slabs leave this open
  // and the stairs zig-zag up through it.
  const float voidW = fminf(96.0f, sx * 0.34f);
  const float voidD = fminf(210.0f, sz * 0.60f);
  const float vx0 = x0 + th, vx1 = vx0 + voidW;
  const float vz1 = z1 - th, vz0 = vz1 - voidD;

  const float sill = 14.0f;                   // window sill height
  const float head = 12.0f;                   // header thickness

  for (int s = 0; s < storeys; ++s) {
    const float by = y + s * storeyH;
    const bool ground = (s == 0);

    AddStoreyShell(x0, z0, x1, z1, by, storeyH, th, ground, wallTex, 0.34f,
                   sill, head);

    // --- ceiling of this storey (= floor of the next, or the roof) ---------
    const float fy = by + storeyH;
    const float ft = 8.0f;
    // Four slabs leaving the stairwell open.
    AddBox({x0, fy - ft, z0}, {vx0, fy, z1}, floorTex, 64.0f);
    AddBox({vx1, fy - ft, z0}, {x1, fy, z1}, floorTex, 64.0f);
    AddBox({vx0, fy - ft, z0}, {vx1, fy, vz0}, floorTex, 64.0f);
    AddBox({vx0, fy - ft, vz1}, {vx1, fy, z1}, floorTex, 64.0f);

    // --- stairs up through the void ---------------------------------------
    // A switchback, not a single straight flight. A straight flight cannot
    // work here: the flight belonging to the storey above starts at this
    // storey's ceiling height at every point along the run, so once you have
    // climbed more than (storeyH - playerHeight) you are walking into it --
    // which is exactly the "stairs meet the floor" you hit.
    //
    // Two half-height flights side by side in X fix it. The half-flight below
    // only ever reaches storeyH/2, which clears the storey above; the upper
    // half-flight is covered by the *upper* half-flight above it, which
    // starts a further storeyH/2 up. A landing joins them at the far end.
    const float half = storeyH * 0.5f;
    const int steps = static_cast<int>(ceilf(half / 5.0f));
    const float vxm = (vx0 + vx1) * 0.5f;
    const float run = voidD - 8.0f;
    const float lz = 26.0f;            // landing depth at the turn

    // Lower flight: -X half of the void, climbing toward +Z.
    AddStairs(vx0 + 3.0f, vz0 + 4.0f, (vxm - vx0) - 4.0f, run - lz, by, half,
              2, steps, "metal");
    // The half-landing you turn around on.
    AddBox({vx0 + 3.0f, by + half - 6.0f, vz1 - 4.0f - lz},
           {vx1 - 3.0f, by + half, vz1 - 4.0f}, "metal", 32.0f);
    // Upper flight: +X half, climbing back toward -Z to the next floor.
    AddStairs(vxm + 1.0f, vz0 + 4.0f, (vx1 - vxm) - 4.0f, run - lz,
              by + half, half, 3, steps, "metal");
    // Top landing, level with the floor slab it delivers you onto.
    AddBox({vxm + 1.0f, fy - 6.0f, vz0 + 4.0f},
           {vx1 - 3.0f, fy, vz0 + 4.0f + lz}, "metal", 32.0f);
  }

  // Roof parapet, so the top is usable cover rather than a bare edge.
  const float ry = y + storeys * storeyH;
  AddWall(x0, z0, x1, z0, ry, 26.0f, 14.0f, wallTex, 32.0f);
  AddWall(x0, z1, x1, z1, ry, 26.0f, 14.0f, wallTex, 32.0f);
  AddWall(x0, z0, x0, z1, ry, 26.0f, 14.0f, wallTex, 32.0f);
  AddWall(x1, z0, x1, z1, ry, 26.0f, 14.0f, wallTex, 32.0f);
}

// GTJ3D's obj_car, measured straight out of its Create event:
//   length 58, width 32, wheel radius 4, wheel thickness 6,
//   wheels at x = 14 and length - 8, cabin from x = 12 to length - 16 with
//   2 units of padding either side and box_height 6.
//   saloon: model_height 7, front_length 25, collision height 20
//   swat van: model_height 18, front_length 5, collision height 30
// Cars used to be baked into the map as static brushes. They are now owned by
// the vehicle system (src/vehicle.cpp) so they can be driven: the map only
// records where they start, and the vehicle system pushes their current
// bounding boxes back through SetVehicleColliders every tick.
void World::SetVehicleColliders(const std::vector<Brush>& boxes) {
  brushes_.resize(staticCount_);
  brushes_.insert(brushes_.end(), boxes.begin(), boxes.end());
}

bool World::Load(const std::string& path) {
  if (!FileExists(path.c_str())) {
    TraceLog(LOG_WARNING, "MAP: %s not found, using built-in fallback", path.c_str());
    BuildFallback();
    return false;
  }
  char* text = LoadFileText(path.c_str());
  if (!text) { BuildFallback(); return false; }

  brushes_.clear();
  spawns_.clear();
  vehicles_.clear();

  std::istringstream file(text);
  std::string line;
  while (std::getline(file, line)) {
    // strip comments
    size_t c = line.find('#');
    if (c != std::string::npos) line = line.substr(0, c);
    std::istringstream in(line);
    std::string cmd;
    if (!(in >> cmd)) continue;

    if (cmd == "name") {
      std::getline(in, name_);
      while (!name_.empty() && name_[0] == ' ') name_.erase(0, 1);
    } else if (cmd == "size") {
      in >> sizeX_ >> sizeZ_;
    } else if (cmd == "ground") {
      in >> groundTex_ >> groundTile_;
    } else if (cmd == "fog") {
      fogColor_ = ParseColor(in, fogColor_);
      in >> fogStart_ >> fogEnd_;
    } else if (cmd == "sky") {
      skyColor_ = ParseColor(in, skyColor_);
    } else if (cmd == "box") {
      float x, y, z, sx, sy, sz, tile = 32.0f;
      std::string tex;
      if (in >> x >> y >> z >> sx >> sy >> sz >> tex) {
        in >> tile;
        AddBox(Vector3{x, y, z}, Vector3{x + sx, y + sy, z + sz}, tex,
               tile > 0 ? tile : 32.0f);
      }
    } else if (cmd == "wall") {
      float x1, z1, x2, z2, y, h, th, tile = 32.0f;
      std::string tex;
      if (in >> x1 >> z1 >> x2 >> z2 >> y >> h >> th >> tex) {
        in >> tile;
        AddWall(x1, z1, x2, z2, y, h, th, tex, tile > 0 ? tile : 32.0f);
      }
    } else if (cmd == "stairs") {
      float x, z, sx, sz, y, h;
      int dir, steps;
      std::string tex;
      if (in >> x >> z >> sx >> sz >> y >> h >> dir >> steps >> tex)
        AddStairs(x, z, sx, sz, y, h, dir, steps, tex);
    } else if (cmd == "tree") {
      // tree <x> <z> <y> <height> [seed]
      float tx, tz, ty, th;
      int seed = 0;
      if (in >> tx >> tz >> ty >> th) {
        if (!(in >> seed)) seed = static_cast<int>(tx * 7.0f + tz * 13.0f);
        AddTree(tx, tz, ty, th, seed);
      }
    } else if (cmd == "building") {
      float bx, bz, bsx, bsz, by, sh;
      int storeys;
      std::string wt, ft;
      if (in >> bx >> bz >> bsx >> bsz >> by >> storeys >> sh >> wt >> ft)
        AddBuilding(bx, bz, bsx, bsz, by, storeys, sh, wt, ft);
    } else if (cmd == "townhouse") {
      // townhouse <x> <z> <sx> <sz> <y> <storeys> <storeyH> <wallTex>
      //           <roofTex> <floorTex>
      float bx, bz, bsx, bsz, by, sh;
      int storeys;
      std::string wt, rt, ft;
      if (in >> bx >> bz >> bsx >> bsz >> by >> storeys >> sh >> wt >> rt >> ft)
        AddTownhouse(bx, bz, bsx, bsz, by, storeys, sh, wt, rt, ft);
    } else if (cmd == "car" || cmd == "tank" || cmd == "heli") {
      // car  <x> <z> <y> <quarterTurn 0-3> <kind> <r> <g> <b>
      //        kind 0 = saloon, 1 = SWAT van, 3 = supercar
      // tank <x> <z> <y> <quarterTurn 0-3> <r> <g> <b>
      VehicleSpawn v;
      float cx, cz, cy;
      int q = 0, kind = 2, r = 255, g = 255, b = 255;
      bool ok;
      if (cmd == "tank" || cmd == "heli") {
        kind = (cmd == "heli") ? 4 : 2;
        ok = static_cast<bool>(in >> cx >> cz >> cy >> q >> r >> g >> b);
      } else {
        ok = static_cast<bool>(in >> cx >> cz >> cy >> q >> kind >> r >> g >> b);
      }
      if (ok) {
        v.pos = Vector3{cx, cy, cz};
        v.yawDeg = static_cast<float>((((q % 4) + 4) % 4) * 90);
        v.kind = kind;
        v.paint = Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
        vehicles_.push_back(v);
      }
    } else if (cmd == "aicar") {
      // aicar <x1> <z1> <x2> <z2> <y> <kind> <r> <g> <b>
      // Traffic that drives back and forth between the two points.
      VehicleSpawn v;
      float ax, az, bx, bz, cy;
      int kind, r, g, b;
      if (in >> ax >> az >> bx >> bz >> cy >> kind >> r >> g >> b) {
        v.pos = Vector3{ax, cy, az};
        v.yawDeg = atan2f(-(bz - az), bx - ax) * RAD2DEG;
        v.kind = kind;
        v.paint = Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
        v.ai = true;
        v.wayA = Vector3{ax, cy, az};
        v.wayB = Vector3{bx, cy, bz};
        vehicles_.push_back(v);
      }
    } else if (cmd == "spawn") {
      SpawnPoint s;
      if (in >> s.pos.x >> s.pos.y >> s.pos.z >> s.yaw) spawns_.push_back(s);
    }
  }
  UnloadFileText(text);

  if (brushes_.empty()) {
    TraceLog(LOG_WARNING, "MAP: %s had no geometry, using fallback", path.c_str());
    BuildFallback();
    return false;
  }
  if (spawns_.empty()) {
    spawns_.push_back(SpawnPoint{Vector3{sizeX_ * 0.5f, 8.0f, sizeZ_ * 0.5f}, 0.0f});
  }
  staticCount_ = brushes_.size();
  ResolveSpawns();
  TraceLog(LOG_INFO, "MAP: '%s' %d brushes, %d spawns, %d vehicles",
           name_.c_str(), (int)brushes_.size(), (int)spawns_.size(),
           (int)vehicles_.size());
  return true;
}

void World::BuildFallback() {
  brushes_.clear();
  spawns_.clear();
  vehicles_.clear();
  sizeX_ = sizeZ_ = 1200.0f;
  // Perimeter + a few blocks so the game is still playable without a map file.
  AddWall(0, 0, sizeX_, 0, 0, 160, 24, "brick", 32);
  AddWall(0, sizeZ_, sizeX_, sizeZ_, 0, 160, 24, "brick", 32);
  AddWall(0, 0, 0, sizeZ_, 0, 160, 24, "brick", 32);
  AddWall(sizeX_, 0, sizeX_, sizeZ_, 0, 160, 24, "brick", 32);
  for (int i = 0; i < 4; ++i) {
    const float x = 250.0f + (i % 2) * 500.0f;
    const float z = 250.0f + (i / 2) * 500.0f;
    AddBox(Vector3{x, 0, z}, Vector3{x + 200, 120, z + 200}, "concrete", 32);
  }
  spawns_.push_back({Vector3{120, 8, 120}, -45.0f});
  spawns_.push_back({Vector3{1080, 8, 1080}, 135.0f});
  spawns_.push_back({Vector3{120, 8, 1080}, 45.0f});
  spawns_.push_back({Vector3{1080, 8, 120}, -135.0f});
  staticCount_ = brushes_.size();
  ResolveSpawns();
}

// A spawn written into a .map file is a pair of numbers somebody typed, and
// several of the Urban Complex ones landed inside the blocks they were meant
// to stand beside -- you started the round sealed in a brick box. Rather than
// only correcting those numbers, every spawn is walked out to open ground at
// load time, so no future edit to any map can put a player inside a wall.
//
// The search is a widening ring. At each candidate the feet are dropped onto
// whatever floor is under them at or below the height that was asked for, so
// a spawn meant for the fifth storey stays on the fifth storey and one meant
// for the street stays on the street.
Vector3 World::FindClearPoint(Vector3 want, float radius, float height) const {
  // A little more room than the capsule really needs, so a spawn is never
  // resolved to a spot the player is immediately stuck against.
  const float r = radius + 2.0f;

  auto tryAt = [&](float x, float z, Vector3* out) {
    if (x < r || z < r || x > sizeX_ - r || z > sizeZ_ - r) return false;
    const float floorY = GroundHeight(x, z, radius, want.y + kStepHeight);
    const Vector3 p{x, floorY, z};
    if (!IsFree(p, r, height)) return false;
    *out = p;
    return true;
  };

  Vector3 found;
  if (tryAt(want.x, want.z, &found)) return found;

  // Nearest first, so a spawn only ever moves as far as it has to.
  for (float ring = 24.0f; ring <= 900.0f; ring += 24.0f) {
    const int steps = 8 + static_cast<int>(ring / 24.0f) * 4;
    for (int i = 0; i < steps; ++i) {
      const float a = (2.0f * PI * i) / steps;
      if (tryAt(want.x + cosf(a) * ring, want.z + sinf(a) * ring, &found))
        return found;
    }
  }
  // Nothing within 900 units is clear, which means the map is solid around
  // here. Leave the point alone rather than teleporting to the far side of
  // the level; at least it stays where the author put it.
  return want;
}

void World::ResolveSpawns() {
  int moved = 0;
  for (SpawnPoint& s : spawns_) {
    const Vector3 fixed = FindClearPoint(s.pos);
    // Only a sideways move means the point was buried. Dropping the feet onto
    // the floor is what happens to every spawn -- map files write a nominal
    // height, not a surveyed one -- and is not worth a warning.
    const float dx = fixed.x - s.pos.x, dz = fixed.z - s.pos.z;
    if (dx * dx + dz * dz > 0.25f) {
      TraceLog(LOG_WARNING,
               "MAP: spawn (%.0f, %.0f, %.0f) was inside geometry, moved to "
               "(%.0f, %.0f, %.0f)",
               s.pos.x, s.pos.y, s.pos.z, fixed.x, fixed.y, fixed.z);
      ++moved;
    }
    s.pos = fixed;
  }
  if (moved > 0)
    TraceLog(LOG_INFO, "MAP: %d of %d spawns dug out of geometry", moved,
             (int)spawns_.size());
}

const SpawnPoint& World::PickSpawn(int seed) const {
  static SpawnPoint fallback{Vector3{0, 8, 0}, 0.0f};
  if (spawns_.empty()) return fallback;
  return spawns_[((seed % (int)spawns_.size()) + (int)spawns_.size()) %
                 (int)spawns_.size()];
}

// ------------------------------------------------------------------ collision

bool World::IsFree(Vector3 feet, float radius, float height) const {
  const Vector3 mn{feet.x - radius, feet.y + 0.5f, feet.z - radius};
  const Vector3 mx{feet.x + radius, feet.y + height, feet.z + radius};
  for (const Brush& b : brushes_) {
    if (Overlap(b, mn, mx)) return false;
  }
  return feet.x > 0.0f && feet.z > 0.0f && feet.x < sizeX_ && feet.z < sizeZ_;
}

Vector3 World::SlideMove(Vector3 feet, Vector3 delta, float radius, float height,
                         bool* hitWall) const {
  bool blocked = false;
  Vector3 p = feet;

  // X then Z so we slide along walls instead of sticking to them.
  if (delta.x != 0.0f) {
    Vector3 t = p; t.x += delta.x;
    if (IsFree(t, radius, height)) {
      p = t;
    } else {
      Vector3 up = t; up.y += kStepHeight;
      if (IsFree(up, radius, height)) p = up;
      else blocked = true;
    }
  }
  if (delta.z != 0.0f) {
    Vector3 t = p; t.z += delta.z;
    if (IsFree(t, radius, height)) {
      p = t;
    } else {
      Vector3 up = t; up.y += kStepHeight;
      if (IsFree(up, radius, height)) p = up;
      else blocked = true;
    }
  }
  if (hitWall) *hitWall = blocked;
  return p;
}

float World::GroundHeight(float x, float z, float radius, float maxY) const {
  float best = 0.0f;   // the street itself
  for (const Brush& b : brushes_) {
    if (x + radius <= b.min.x || x - radius >= b.max.x) continue;
    if (z + radius <= b.min.z || z - radius >= b.max.z) continue;
    if (b.max.y <= maxY && b.max.y > best) best = b.max.y;
  }
  return best;
}

float World::CeilingHeight(float x, float z, float radius, float minY) const {
  float best = 100000.0f;
  for (const Brush& b : brushes_) {
    if (x + radius <= b.min.x || x - radius >= b.max.x) continue;
    if (z + radius <= b.min.z || z - radius >= b.max.z) continue;
    if (b.min.y >= minY && b.min.y < best) best = b.min.y;
  }
  return best;
}

// ------------------------------------------------------------------ raycasts

RayHit World::Raycast(Vector3 origin, Vector3 dir, float maxDist) const {
  RayHit out;
  float nearest = maxDist;

  const float inv[3] = {
      dir.x != 0.0f ? 1.0f / dir.x : 1e30f,
      dir.y != 0.0f ? 1.0f / dir.y : 1e30f,
      dir.z != 0.0f ? 1.0f / dir.z : 1e30f,
  };
  const float o[3] = {origin.x, origin.y, origin.z};

  for (size_t i = 0; i < brushes_.size(); ++i) {
    const Brush& b = brushes_[i];
    const float mn[3] = {b.min.x, b.min.y, b.min.z};
    const float mx[3] = {b.max.x, b.max.y, b.max.z};

    float t0 = 0.0f, t1 = nearest;
    int axis = -1;
    float sign = 1.0f;
    bool miss = false;
    for (int a = 0; a < 3; ++a) {
      float ta = (mn[a] - o[a]) * inv[a];
      float tb = (mx[a] - o[a]) * inv[a];
      float s = -1.0f;
      if (ta > tb) { std::swap(ta, tb); s = 1.0f; }
      if (ta > t0) { t0 = ta; axis = a; sign = s; }
      if (tb < t1) t1 = tb;
      if (t0 > t1) { miss = true; break; }
    }
    if (miss || t0 < 0.0f || t0 >= nearest) continue;

    nearest = t0;
    out.hit = true;
    out.dist = t0;
    out.brushIndex = static_cast<int>(i);
    out.normal = Vector3{0, 0, 0};
    if (axis == 0) out.normal.x = sign;
    else if (axis == 1) out.normal.y = sign;
    else if (axis == 2) out.normal.z = sign;
    else out.normal.y = 1.0f;
  }

  // Street plane.
  if (dir.y < -0.0001f) {
    const float t = -origin.y / dir.y;
    if (t > 0.0f && t < nearest) {
      nearest = t;
      out.hit = true;
      out.dist = t;
      out.brushIndex = -1;
      out.normal = Vector3{0, 1, 0};
    }
  }

  if (out.hit) out.point = Vector3Add(origin, Vector3Scale(dir, out.dist));
  return out;
}

bool World::LineOfSight(Vector3 a, Vector3 b) const {
  Vector3 d = Vector3Subtract(b, a);
  const float len = Vector3Length(d);
  if (len < 0.001f) return true;
  d = Vector3Scale(d, 1.0f / len);
  const RayHit h = Raycast(a, d, len);
  return !h.hit;
}

}  // namespace kaj
