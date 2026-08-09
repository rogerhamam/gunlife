// Kaj's Shooter Game 3D -- shared types and tuning constants.
//
// Units, speeds and physics constants are inherited from Grand Theft Jack 3D
// (GTJ3D.gmk) so the game moves and shoots the same way. GTJ ran a fixed 60
// step/second room, and every constant below is expressed per *tick* exactly as
// it was in the GML.
//
// Coordinate system: raylib native, +Y up. GTJ's (x, y, z) maps to (x, z, y),
// so a GTJ heading of `direction` degrees is a forward vector of
// (cos yaw, 0, -sin yaw) -- identical maths, just a renamed axis.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "raylib.h"
#include "raymath.h"

namespace kaj {

// ---------------------------------------------------------------- simulation
constexpr int   kTickRate    = 60;                  // GTJ room_speed
constexpr float kTickDt      = 1.0f / kTickRate;

// ------------------------------------------------------------------- physics
// Straight out of obj_player's Create/Step events.
constexpr float kGravity     = 0.15f;   // per tick^2
constexpr float kJumpSpeed   = 2.6f;    // per tick
constexpr float kRunSpeed    = 3.0f;    // per tick
constexpr float kSneakSpeed  = 1.0f;    // ctrl/shift
constexpr float kStrafeSpeed = 2.0f;    // A/D, applied directly like GTJ
constexpr float kAccel       = 0.4f;    // speed ramp per tick
constexpr float kFriction    = 0.2f;    // speed decay per tick

// Player capsule. GTJ used rad 7 / height 14 with the eye exactly on top; we
// keep the radius, grow the box slightly so a head region exists to shoot at.
constexpr float kPlayerRadius     = 7.0f;
constexpr float kPlayerHeight     = 20.0f;
constexpr float kPlayerEye        = 15.0f;
constexpr float kCrouchHeight     = 12.0f;
constexpr float kCrouchEye        = 9.0f;
constexpr float kCrouchSpeedScale = 0.45f;
constexpr float kStepHeight       = 6.0f;   // auto-step onto kerbs and crates

// Hit zones as a fraction of the standing capsule, CS-style multipliers.
constexpr float kHeadZone   = 0.78f;   // above this fraction of height = head
constexpr float kLegZone    = 0.30f;   // below this fraction = legs
constexpr float kHeadMult   = 4.0f;
constexpr float kLegMult    = 0.75f;

constexpr float kMaxHealth = 100.0f;
constexpr float kMaxArmor  = 100.0f;

// GTJ clamped pitch to +/-60. The brief asks for unrestricted look, so we only
// stop short of straight up/down to keep the view matrix well defined.
constexpr float kPitchLimit = 89.0f;

constexpr float kFovY        = 70.0f;   // GTJ d3d_set_projection_ext fov
constexpr float kRespawnTime = 3.0f;    // seconds

// -------------------------------------------------------------------- render
// GTJ drew its HUD and viewmodel through d3d_set_projection_ortho(0,0,640,480).
constexpr float kHudW = 640.0f;
constexpr float kHudH = 480.0f;

// ------------------------------------------------------------------- weapons
enum WeaponId : uint8_t {
  WEAPON_FISTS = 0,
  WEAPON_PISTOL,
  WEAPON_SMG,
  WEAPON_RIFLE,       // full auto; the semi-auto marksman rifle was dropped
  WEAPON_SHOTGUN,
  WEAPON_SUPERSHOTGUN,
  WEAPON_SNIPER,
  WEAPON_ROCKET,
  WEAPON_GRENADE,
  WEAPON_MINE,
  WEAPON_TRIPFLARE,
  WEAPON_COUNT
};

enum FireMode : uint8_t { FIRE_MELEE, FIRE_SEMI, FIRE_AUTO, FIRE_PROJECTILE, FIRE_PLACE };

// --------------------------------------------------------------------- misc
inline float DegToRadF(float d) { return d * (PI / 180.0f); }

// GTJ heading convention, lifted to 3D.
inline Vector3 ForwardFromAngles(float yawDeg, float pitchDeg) {
  const float y = DegToRadF(yawDeg), p = DegToRadF(pitchDeg);
  const float cp = cosf(p);
  return Vector3{cosf(y) * cp, sinf(p), -sinf(y) * cp};
}

inline Vector3 FlatForward(float yawDeg) {
  const float y = DegToRadF(yawDeg);
  return Vector3{cosf(y), 0.0f, -sinf(y)};
}

// Right-hand strafe vector matching GTJ's `x + sin(dir)*2, y + cos(dir)*2`.
inline Vector3 FlatRight(float yawDeg) {
  const float y = DegToRadF(yawDeg);
  return Vector3{sinf(y), 0.0f, cosf(y)};
}

inline float Clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

inline float RandRange(float lo, float hi) {
  return lo + (hi - lo) * (static_cast<float>(GetRandomValue(0, 100000)) / 100000.0f);
}

}  // namespace kaj
