// Player state (networked) and the local first-person controller.
//
// Movement reproduces obj_player's Step event from GTJ3D: a scalar `speed`
// accelerated by 0.4/tick toward +/-3 with GameMaker's 0.2/tick friction,
// A/D applying a flat 2 units of strafe, and z physics of
// `z += zspeed; zspeed -= 0.15` with a 2.6 jump impulse.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "common.h"
#include "weapons.h"

namespace kaj {

class World;

enum PlayerFlags : uint8_t {
  PF_CROUCH   = 1 << 0,
  PF_DEAD     = 1 << 1,
  PF_ONGROUND = 1 << 2,
  PF_FIRING   = 1 << 3,
  PF_RELOAD   = 1 << 4,
  PF_ZOOM     = 1 << 5,
};

// Replicated player record. Kept POD so it can be memcpy'd into packets.
struct PlayerState {
  uint8_t id = 0;
  uint8_t active = 0;
  uint8_t team = 0;
  uint8_t weapon = WEAPON_PISTOL;
  uint8_t flags = 0;
  uint8_t pad = 0;
  int16_t kills = 0;
  int16_t deaths = 0;
  float x = 0, y = 0, z = 0;
  float yaw = 0, pitch = 0;
  float health = kMaxHealth;
  float armor = 0;
  char name[24] = {0};

  Vector3 pos() const { return Vector3{x, y, z}; }
  void setPos(Vector3 p) { x = p.x; y = p.y; z = p.z; }
  bool dead() const { return (flags & PF_DEAD) != 0; }
  bool crouching() const { return (flags & PF_CROUCH) != 0; }
  float height() const { return crouching() ? kCrouchHeight : kPlayerHeight; }
  float eye() const { return crouching() ? kCrouchEye : kPlayerEye; }
  Vector3 eyePos() const { return Vector3{x, y + eye(), z}; }
};

// What the local machine sends upstream each tick.
struct InputCommand {
  float moveForward = 0;   // -1..1
  float moveStrafe = 0;    // -1..1
  bool jump = false;
  bool crouch = false;
  // Ctrl alone, without the `C` alias crouch also answers to. The gunship's
  // collective is on this: `C` toggles the chase camera while you are in a
  // vehicle, so it cannot also be the thing that makes you descend.
  bool descend = false;
  bool sneak = false;
  bool fire = false;
  bool firePressed = false;
  bool reload = false;
  bool zoom = false;
  int  weaponWheel = 0;    // -1 / +1
  int  weaponDirect = -1;  // number-key selection
  bool usePressed = false; // E: get into / out of a vehicle
};

class LocalPlayer {
 public:
  void Reset(Vector3 spawnPos, float spawnYaw);
  // One fixed 60 Hz simulation step.
  void Tick(const InputCommand& in, const World& world);

  void ApplyRecoil(float degrees);
  void Damage(float amount);

  Vector3 pos{};        // feet
  float yaw = 0.0f;
  float pitch = 0.0f;

  float speed = 0.0f;   // GTJ scalar along `yaw`
  float vy = 0.0f;
  bool onGround = true;
  bool crouch = false;
  bool dead = false;

  float health = kMaxHealth;
  float armor = 0.0f;
  Arsenal arsenal;

  // View feel
  float bobPhase = 0.0f;
  float bobAmount = 0.0f;
  float bobHeight = 0.0f;   // smoothed vertical offset, applied to the eye
  float recoilPitch = 0.0f;
  float landDip = 0.0f;
  // Downward speed at the moment of touchdown, non-zero for exactly the tick
  // you land. Game turns it into fall damage; the controller itself has no
  // business knowing about health.
  float landImpact = 0.0f;
  float zoomT = 0.0f;       // 0 = hip, 1 = fully scoped
  bool zoomed = false;
  // Crouch is interpolated rather than snapped, so the camera sinks instead of
  // teleporting down.
  float crouchT = 0.0f;

  float height() const {
    return kPlayerHeight + (kCrouchHeight - kPlayerHeight) * crouchT;
  }
  float eye() const {
    return kPlayerEye + (kCrouchEye - kPlayerEye) * crouchT;
  }
  Vector3 eyePos() const {
    return Vector3{pos.x, pos.y + eye() - landDip + bobHeight, pos.z};
  }
  float viewPitch() const { return Clampf(pitch + recoilPitch, -kPitchLimit, kPitchLimit); }
  Vector3 aimDir() const { return ForwardFromAngles(yaw, viewPitch()); }

  // Cone of fire in degrees for the currently held weapon.
  float CurrentSpread() const;

  // Damage for a landing at `impact` units per tick of downward speed. Zero
  // for anything you could reach with your own jump.
  static float FallDamage(float impact);

 private:
  float prevVy_ = 0.0f;
};

}  // namespace kaj
