// Particle effects: smoke, fire, debris, shrapnel, shockwaves, tracers, decals.
//
// The recipe follows Naval Command's explosion stack -- a shockwave, a bright
// core flash, an outward eruption of fire chunks and smoke, chaos lobes that
// break the silhouette, a bulbous smoke cloud that blooms at full radius, then
// debris and shrapnel -- rebuilt here on raylib with procedurally generated
// particle textures. Nothing in this file uses a GTJ3D sprite.
//
// Particle physics per Naval Command's SmokeSystem::advance: buoyancy, an
// exponential drag retention factor, and coherent-noise turbulence applied to
// smoke only, so flashes stay crisp while smoke swirls and breaks up.
#pragma once

#include <cstdint>
#include <vector>

#include "common.h"

namespace kaj {

class World;
class Assets;

// A box whose front/back faces wear a horizontal band of a character sheet and
// whose sides wear a narrow slice of it, so a flat GTJ3D sprite wraps around a
// solid model. uv0/uv1 select the band, top to bottom.
void DrawSkinnedBox(Vector3 center, Vector3 size, const Texture2D& tex,
                    float uv0, float uv1, Color tint);

// Palette for the SWAT operator the enemies are built from. Kept here so the
// live model and the corpse it becomes are cut from the same cloth.
namespace swat {
constexpr Color kSuit{20, 21, 24, 255};        // black tactical fatigues
constexpr Color kVest{31, 33, 38, 255};        // plate carrier, a shade lighter
constexpr Color kRig{44, 46, 52, 255};         // webbing, pouches
constexpr Color kHelmet{16, 17, 20, 255};
constexpr Color kSkin{206, 168, 138, 255};
constexpr Color kVisor{38, 44, 54, 255};
constexpr Color kBoot{12, 12, 14, 255};
}  // namespace swat

// Draws the SWAT figure, standing, facing +X before the caller's yaw rotation.
// `height` is the full standing height in world units.
void DrawSwatFigure(float height, Color tint);

// One metre in world units. GTJ3D's player is 20 units for ~1.8 m, so Naval
// Command's metre-based numbers are scaled through this.
constexpr float kUnitsPerMetre = 11.0f;

struct Particle {
  Vector3 pos{}, vel{};
  float age = 0.0f, life = 1.0f, seed = 0.0f;
  float startSize = 4.0f, endSize = 20.0f;
  float alphaPeak = 0.7f;
  float tint = 0.0f;        // 0 = smoke/dust, 1 = fire
  float buoy = 0.0f;        // upward acceleration, units/s^2
  float drag = 0.5f;        // velocity retained per second
  bool noTurb = false;
  Color base = WHITE;       // dust and sparks override the default palettes
};

struct DebrisChunk {
  Vector3 pos{}, vel{};
  Vector3 spin{}, angle{};
  float size = 3.0f;
  float age = 0.0f, life = 6.0f;
  bool onFire = false;
  Color color = Color{34, 30, 27, 255};
  // Velocity retained per second. Blast debris is heavily damped so it does
  // not sail across the map; a spent case is not, so it throws up and falls
  // back in a clean arc.
  float drag = 0.55f;
  bool casing = false;
  int bounces = 0;
};

struct Shrapnel {
  Vector3 pos{}, prev{}, vel{};
  float age = 0.0f, life = 0.8f;
  float size = 1.2f;
};

struct Shockwave {
  Vector3 pos{};
  float radius = 0.0f, maxRadius = 100.0f;
  float age = 0.0f, life = 0.45f;
};

// A round in flight. The visible streak is a short segment that travels from
// `a` toward `b` at `speed`, so the tracer arrives when the bullet does.
struct Tracer {
  Vector3 a{}, b{};
  Vector3 dir{};
  float total = 0.0f;      // full distance a -> b
  float travelled = 0.0f;
  float speed = 9000.0f;   // world units per second
  float streak = 90.0f;    // length of the visible segment
  float life = 0.0f, maxLife = 1.2f;
  float width = 0.5f;
  Color color = Color{255, 224, 150, 255};
};

struct Decal {
  Vector3 pos{};
  Vector3 normal{0, 1, 0};
  Vector3 right{1, 0, 0};
  float size = 6.0f;
  // Half-extents along `right` and up, clamped to the face the decal sits on
  // so a hole near an edge is cut off there instead of hanging out over the
  // corner into thin air.
  float halfX = 6.0f, halfY = 6.0f;
  float life = 0.0f, maxLife = 20.0f;
  Color color = Color{20, 16, 14, 255};
  bool hole = false;       // bullet hole art rather than a soft scorch
};

// A body part thrown by a death. Parts are rigid boxes with their own spin,
// gravity, drag and ground bounce -- enough to read as a ragdoll coming apart
// without a full constraint solver.
struct BodyPart {
  Vector3 pos{}, vel{};
  Vector3 angle{}, spin{};
  Vector3 size{4, 4, 4};
  float age = 0.0f, life = 12.0f;
  Color color = Color{150, 140, 130, 255};
  bool bleeds = false;     // trails blood while airborne
  bool grounded = false;
  // Optional texture band from the character sheet, so torso/head keep the
  // GTJ3D artwork while they tumble.
  const Texture2D* skin = nullptr;
  float uv0 = 0.0f, uv1 = 1.0f;
};

class FxSystem {
 public:
  bool Init();
  void Shutdown();
  void Clear();

  // --- emitters -----------------------------------------------------------
  // Generic burst, mirroring Naval Command's emit_burst signature.
  void EmitBurst(Vector3 pos, Vector3 dir, int count, float lifeAvg,
                 float sizeStart, float sizeEnd, float alphaPeak, float tint,
                 float buoy, float drag, Vector3 carryVel = Vector3{0, 0, 0},
                 float posJitter = 1.0f);
  // Fire chunks + smoke shot outward in random 3D directions.
  void EmitEruption(Vector3 c, float scale, int nFire, int nSmoke, float baseSpeed);
  // Random sub-fireballs that break up a tidy ball.
  void EmitChaosLobes(Vector3 c, float scale, int n, float lifeBase);
  // Big soft smoke that blooms at near-full radius immediately and lingers.
  void EmitBulbousCloud(Vector3 c, float scale, int n, float lifeBase);

  void SpawnDebris(Vector3 pos, int count, float scaleMul, float fireChance,
                   float baseSpeed);
  void SpawnShrapnel(Vector3 pos, int count, float scale, float baseSpeed);
  void AddShockwave(Vector3 pos, float maxRadius);

  // The full composite. `radius` is the blast radius in world units.
  void SpawnExplosion(Vector3 pos, float radius, bool nearGround);
  // Small stuff.
  void MuzzleFlash(Vector3 pos, Vector3 dir, float scale, float smokeMul = 1.0f);
  void ImpactPuff(Vector3 pos, Vector3 normal, float scale, Color dust);
  void BloodPuff(Vector3 pos, Vector3 dir, float scale);
  // Spray out the far side of a hit, plus a mist at the entry.
  void BloodSpray(Vector3 pos, Vector3 shotDir, float scale);
  // Exhaust behind a rocket in flight: burning core plus a smoke column that
  // hangs in the air along the flight path.
  void RocketTrail(Vector3 pos, Vector3 back, float dt);
  void AddTracer(Vector3 a, Vector3 b, Color c, float width, float speed);
  // A tank shell: the same travelling tracer as a bullet, but fat, yellow and
  // slow enough to watch, with a smoke trail laid along its path.
  void ShellTracer(Vector3 a, Vector3 b, Vector3 dir);
  // A spent case thrown clear of the breech: a small brass polygon that
  // tumbles, bounces and stays on the ground. `scale` sizes the case.
  void EjectCasing(Vector3 pos, Vector3 shotDir, float scale);
  void AddDecal(Vector3 p, Vector3 n, float size, Color c, float life);
  // `faceMin`/`faceMax` are the bounds of the surface struck; the mark is
  // trimmed to them so it never overhangs an edge.
  void AddBulletHole(Vector3 p, Vector3 n, Vector3 faceMin, Vector3 faceMax);

  // --- corpses ------------------------------------------------------------
  // Blows a body apart from `hitPos` along `shotDir`. `gib` turns a shot death
  // into an explosive one: every part separates and viscera goes everywhere.
  void SpawnCorpse(Vector3 feet, float yaw, float height, Color teamColor,
                   const Texture2D* skin, Vector3 hitPos, Vector3 shotDir,
                   float force, bool headshot, bool gib);

  // --- lifecycle ----------------------------------------------------------
  void Update(float dt, float elapsed, const World& world, Vector3 wind);
  void Draw(const Camera3D& cam);

  int particleCount() const { return static_cast<int>(parts_.size()); }
  int decalCount() const { return static_cast<int>(decals_.size()); }

  // Where spent cases hit the ground since the last call, so the caller can
  // play the impact sound positionally. Fx owns no audio of its own.
  std::vector<Vector3> TakeCasingHits() {
    std::vector<Vector3> out;
    out.swap(casingHits_);
    return out;
  }

 private:
  void DrawParticles(const Camera3D& cam);

  std::vector<Particle> parts_;
  std::vector<DebrisChunk> debris_;
  std::vector<Shrapnel> shrap_;
  std::vector<Shockwave> shocks_;
  std::vector<Tracer> tracers_;
  std::vector<Decal> decals_;
  std::vector<BodyPart> bodyParts_;
  std::vector<Vector3> casingHits_;

  // Sorted draw order (indices into parts_), rebuilt each frame.
  std::vector<std::pair<float, int>> order_;

  Texture2D puff_{};      // soft radial blob
  Texture2D spark_{};     // tight hot core
  Texture2D hole_{};      // bullet hole: dark core, bright rim
  bool ready_ = false;

  static constexpr int kMaxParticles = 4200;
};

}  // namespace kaj
