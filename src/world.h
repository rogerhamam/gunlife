// The map: axis-aligned brushes, a ground plane, spawn points and collision.
//
// Geometry is authored as line "walls" with a thickness, exactly like GTJ3D's
// obj_wall (x1,y1 -> x2,y2 with a height), plus boxes for crates, roofs and
// platforms. Everything collapses to AABBs, which keeps collision and raycasts
// trivial and fast.
#pragma once

#include <string>
#include <vector>

#include "common.h"

namespace kaj {

class Assets;

struct Brush {
  Vector3 min{};
  Vector3 max{};
  std::string tex = "concrete";
  float tile = 32.0f;   // world units per texture repeat (GTJ used 32)
  Color tint = WHITE;
  bool climbable = true;   // false => bullets pass but players cannot stand
  // Vehicles push their bounding box in here each tick so that collision,
  // ground height and raycasts all treat them as solid for free -- but they
  // draw themselves, oriented, so the renderer skips these.
  bool invisible = false;
};

// A vehicle placed by the map. These are not baked into geometry: the vehicle
// system owns them so they can be driven.
struct VehicleSpawn {
  Vector3 pos{};       // ground point under the chassis centre
  float yawDeg = 0.0f;
  int kind = 0;        // 0 saloon, 1 SWAT van, 2 tank, 3 supercar
  Color paint = WHITE;
  // Traffic: drives back and forth between wayA and wayB until someone gets in.
  bool ai = false;
  Vector3 wayA{}, wayB{};
};

struct SpawnPoint {
  Vector3 pos{};
  float yaw = 0.0f;
};

struct RayHit {
  bool hit = false;
  float dist = 0.0f;
  Vector3 point{};
  Vector3 normal{};
  int brushIndex = -1;
};

class World {
 public:
  bool Load(const std::string& path);
  void BuildFallback();   // used if the map file is missing

  // --- collision -----------------------------------------------------------
  // True when a capsule (approximated as a box) at `feet` touches nothing.
  bool IsFree(Vector3 feet, float radius, float height) const;
  // Horizontal slide with automatic step-up over kerbs and crates.
  Vector3 SlideMove(Vector3 feet, Vector3 deltaXZ, float radius, float height,
                    bool* hitWall) const;
  // Highest walkable surface at or below `maxY` under the given footprint.
  float GroundHeight(float x, float z, float radius, float maxY) const;
  // Lowest ceiling at or above `minY`.
  float CeilingHeight(float x, float z, float radius, float minY) const;

  // `ignoreBrush` skips one brush entirely -- used to let a vehicle's own
  // gun shoot past its own collider. A vehicle pushes its bounding box into
  // the world so collision treats it as solid, and the tank's roof gun fires
  // from an eye three units above the top of that box: aim down even slightly
  // and the first thing the round hits is the tank firing it.
  RayHit Raycast(Vector3 origin, Vector3 dir, float maxDist,
                 int ignoreBrush = -1) const;
  // True when nothing solid blocks the segment (used for explosion line of sight).
  bool LineOfSight(Vector3 a, Vector3 b) const;

  // The nearest point to `want` where a player capsule fits and has a floor
  // under it. Returns `want` unchanged when it was already clear. Used to
  // guarantee that nothing -- a spawn, a respawn, a bot, a story-mode wave --
  // is ever placed inside geometry, however the map was authored.
  Vector3 FindClearPoint(Vector3 want, float radius = kPlayerRadius,
                         float height = kPlayerHeight) const;

  // --- accessors -----------------------------------------------------------
  const std::vector<Brush>& brushes() const { return brushes_; }
  const std::vector<SpawnPoint>& spawns() const { return spawns_; }
  const std::vector<VehicleSpawn>& vehicleSpawns() const { return vehicles_; }
  const SpawnPoint& PickSpawn(int seed) const;

  // Replaces the trailing run of vehicle colliders. Everything before
  // staticCount_ is map geometry and is never touched.
  void SetVehicleColliders(const std::vector<Brush>& boxes);
  // Where that run starts. A raycast that comes back with a brush index at or
  // above this hit a vehicle, and its offset from here is which one.
  int staticBrushCount() const { return static_cast<int>(staticCount_); }

  const std::string& name() const { return name_; }
  float sizeX() const { return sizeX_; }
  float sizeZ() const { return sizeZ_; }
  Color fogColor() const { return fogColor_; }
  float fogStart() const { return fogStart_; }
  float fogEnd() const { return fogEnd_; }
  Color skyColor() const { return skyColor_; }
  const std::string& groundTex() const { return groundTex_; }
  float groundTile() const { return groundTile_; }

 private:
  void AddWall(float x1, float z1, float x2, float z2, float y, float height,
               float thickness, const std::string& tex, float tile);
  void AddBox(Vector3 mn, Vector3 mx, const std::string& tex, float tile);
  void AddStairs(float x, float z, float sx, float sz, float y, float height,
                 int dir, int steps, const std::string& tex);
  // A multi-storey building you can walk into: outer walls with a window per
  // side, a doorway at ground level, floor slabs, and a stairwell running up
  // one corner to a flat roof.
  void AddBuilding(float x, float z, float sx, float sz, float y, int storeys,
                   float storeyH, const std::string& wallTex,
                   const std::string& floorTex);
  // A house: the same enterable shell as AddBuilding but at domestic scale,
  // with a straight stair instead of a switchback and a pitched roof instead
  // of a flat one. The roof is a stack of narrowing slabs -- brushes are
  // axis-aligned boxes, so a true wedge is not available, and at this art
  // level a five-step pitch reads as a gable from anywhere you can stand.
  void AddTownhouse(float x, float z, float sx, float sz, float y, int storeys,
                    float storeyH, const std::string& wallTex,
                    const std::string& roofTex, const std::string& floorTex);
  // Outer walls for one storey with a centred opening per side: a doorway at
  // ground level on the -X face, a window everywhere else. Shared by
  // AddBuilding and AddTownhouse, which differ only above the ceiling.
  void AddStoreyShell(float x0, float z0, float x1, float z1, float by,
                      float storeyH, float th, bool ground,
                      const std::string& wallTex, float openFrac, float sill,
                      float head);
  // A tree: a bark trunk carrying a crown of overlapping leaf slabs. Kingdom
  // (DARK CROWN) builds its trees the same way -- the art that ships with it
  // is the two tiling skins, not a mesh -- and brushes mean a tree here is
  // ordinary world geometry: solid, shootable, fogged and lit like the rest.
  // `seed` shakes the crown around so a row of them is not stamped out.
  void AddTree(float x, float z, float y, float height, int seed);
  // Walks every spawn point out of whatever it was buried in. Called once the
  // map's geometry is complete.
  void ResolveSpawns();
  std::string name_ = "Urban Complex";
  float sizeX_ = 2400.0f;
  float sizeZ_ = 2400.0f;
  std::vector<Brush> brushes_;
  std::vector<SpawnPoint> spawns_;
  std::vector<VehicleSpawn> vehicles_;
  size_t staticCount_ = 0;   // brushes_ entries that belong to the map itself

  // GTJ's default atmosphere: make_color_rgb(0,192,192) with fog 500..3000.
  Color fogColor_{0, 120, 132, 255};
  float fogStart_ = 400.0f;
  float fogEnd_ = 2000.0f;
  Color skyColor_{22, 30, 38, 255};
  std::string groundTex_ = "floor";
  float groundTile_ = 128.0f;
};

}  // namespace kaj
