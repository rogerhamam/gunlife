// Drivable vehicles.
//
// The handling model is Grand Theft Jack 3D's obj_car, step for step, running
// on the same fixed 60 Hz tick GTJ3D used -- so the numbers below are its
// numbers, not re-tuned equivalents:
//
//   accel        random_range(0.005, 0.02)   per car, rolled once
//   maxspeed     irandom_range(10, 14)       per car, rolled once
//   brake        0.05
//   turning      3 degrees per step, only while |speed| > 0
//   grip         0.1     (how fast the camera swings onto the new heading)
//   reverse      4
//   friction     0.01 while a control is held, 0.05 coasting
//
// and the acceleration curve is GTJ's, which approaches maxspeed rather than
// ramping linearly:  speed += abs(maxspeed - speed) * (accel + friction).
//
// Entry, exit and the crash rules are lifted from obj_player's car block: E to
// get in when you are within the car's `size` and facing it inside 45 degrees,
// a one-second door-and-start delay, space to bail out with an upward kick.
#pragma once

#include <string>
#include <vector>

#include "common.h"
#include "world.h"

namespace kaj {

class Assets;

enum VehicleKind : int {
  VEH_SALOON = 0,
  VEH_VAN = 1,
  VEH_TANK = 2,
  VEH_SUPER = 3,   // rare: quicker off the line, higher top speed, fragile
  VEH_HELI = 4,    // gunship: flies, minigun and rocket pods
  VEH_KINDS = 5,
};

struct VehicleDef {
  const char* name;
  float length, width, modelHeight, frontLength;
  float height;        // total collision height
  float brake, turning, grip, reverseSpeed;
  float accelMin, accelMax;
  float speedMin, speedMax;
  float maxLife;
  float seatHeight;    // eye offset above the chassis floor while driving
  float size;          // GTJ obj_car `size`: the interact radius
};

const VehicleDef& VehicleInfo(int kind);

// How many operators a hostile SWAT van carries. obj_car rolled
// `irandom_range(4, 6)`; a full squad reads better as a set-piece arrival.
constexpr int kSwatSquad = 8;

// What the driver is holding down this tick.
//
// The gunship reads the same four movement keys a car does, but as a cyclic
// rather than a throttle:
//
//   W / S    nose down / nose up  -> fly forward / back
//   A / D    bank left / right    -> slide sideways
//   Space    collective up        -> climb
//   Ctrl/C   collective down      -> descend
//   Mouse    look, and the nose follows your yaw
//   E        get out (Space is the collective, so it cannot also be the door)
//
// The previous scheme put the collective on W/S and took forward thrust from
// how far *down you were looking*, which meant the only things W and S did
// were up and down, and flying anywhere required staring at the floor.
struct DriveInput {
  bool fwd = false, back = false, left = false, right = false;
  bool climb = false, sink = false;   // helicopter collective
  // Where the nose should point. The mouse still yaws the airframe; its pitch
  // is look only and no longer flies the machine.
  float heading = 0.0f;
  bool haveAir = false;
};

struct Vehicle {
  int kind = VEH_SALOON;
  Color paint = WHITE;
  Vector3 pos{};          // ground point under the chassis centre
  float dirDeg = 0.0f;    // GTJ heading: 0 = +X, counter-clockwise positive
  float speed = 0.0f;     // world units per 60 Hz tick
  float friction = 0.05f;
  float wheelSpin = 0.0f, wheelAngle = 0.0f, aimWheelAngle = 0.0f;
  float life = 100.0f;
  int driver = -1;        // player id at the wheel, -1 = empty
  bool engineOn = false;

  // Tank only: the turret traverses independently of the hull, so the mouse
  // aims the gun while A/D steer the tracks underneath it.
  float turretDeg = 0.0f;

  // Helicopter only. GTJ3D's obj_enemy flew its helicopter by pushing z_speed
  // toward a hover ceiling (z<120 climb, above it settle, capped at 1 per
  // step); the same numbers drive this, with the collective under the pilot.
  float vy = 0.0f;         // vertical speed, world units per tick
  float rotor = 0.0f;      // blade angle, degrees
  float spool = 0.0f;      // 0 = stopped, 1 = at flight rpm
  float pitchDeg = 0.0f;   // nose attitude, drives forward thrust
  float rollDeg = 0.0f;    // bank, cosmetic + strafe

  // AI patrol: drives back and forth between two points until someone gets
  // in, at which point it becomes an ordinary vehicle.
  bool ai = false;
  Vector3 wayA{}, wayB{};
  bool towardB = true;

  // Story mode: this one is hunting you. A hostile tank closes and shells
  // you; a hostile gunship stands off at height and works you over with the
  // minigun. Both drive themselves through the same handling model the player
  // gets, so they corner and climb the way a driven one does.
  bool hostile = false;
  int fireCooldown = 0;
  int burst = 0;               // rounds left in the current minigun burst
  int salvo = 0;               // bursts fired, so every third can be a rocket
  // Set for one tick when the vehicle wants to shoot. VehicleSystem does not
  // own effects or health, so Game reads these and turns them into a tracer,
  // a blast and damage.
  int firedWeapon = -1;        // -1 nothing, 0 cannon, 1 machine gun
  Vector3 fireFrom{}, fireAt{};

  // SWAT van only. obj_car's swat block: it drove at you carrying `occupants`
  // and, once it was close enough, stopped dead and emptied them into the
  // street. Same here, at eight -- a full squad.
  int occupants = 0;
  // Set to the number to put on the ground for exactly one tick, once the van
  // has stopped. Game does the spawning, because the operators are server bot
  // slots and VehicleSystem has no business knowing about those.
  int dropOff = 0;
  bool deployed = false;

  // Non-zero for one tick after a collision, so the caller can shake the
  // screen and play the crash sound without the vehicle owning audio.
  float crashImpulse = 0.0f;
};

class VehicleSystem {
 public:
  // Rebuilds the fleet from the map. Safe to call on every map change.
  void Reset(const World& world);

  // One 60 Hz step for every vehicle. `driven` is the index the local player
  // is in, or -1. Pushes fresh colliders into the world at the end.
  void Tick(World& world, int driven, const DriveInput& in);

  // Where the hostile vehicles should be pointing themselves. Set once per
  // tick before Tick; clearing it makes them hold station.
  void SetThreat(Vector3 eyePos, bool have) {
    threat_ = eyePos;
    haveThreat_ = have;
  }

  // Adds a hostile vehicle at a clear point near `near`, facing the threat.
  // Returns its index, or -1 if there was nowhere to put it.
  int SpawnHostile(const World& world, int kind, Vector3 near, Color paint);
  // Drops every hostile vehicle -- between waves, and when a story run ends.
  void ClearHostiles();
  int hostileCount() const;

  // Damage a vehicle by the index of the collider brush a shot landed on.
  // Returns true if that brush really was a vehicle. World keeps vehicle
  // colliders in a run at the end of its brush list, in the same order these
  // were pushed, which is what makes the mapping possible at all.
  bool DamageByBrush(const World& world, int brushIndex, float damage,
                     int* outVehicle);
  // Which world brush is this vehicle's collider, so a shot fired from inside
  // it can be told to ignore it. -1 when it has none (destroyed, or the
  // colliders have not been pushed yet this tick).
  int ColliderBrush(const World& world, int vehicle) const;

  // Index of the vehicle the player at `feet` looking along `yawDeg` can get
  // into, or -1. Mirrors GTJ's distance + 45 degree facing test.
  int FindEnterable(Vector3 feet, float yawDeg) const;

  // Where the driver's eye sits, and the heading the chassis is on.
  Vector3 SeatPos(int index) const;
  // Muzzle of the tank's main gun, given the turret's current heading.
  Vector3 GunMuzzle(int index, float extraHeight) const;

  // Pushes a launch point out along `dir` until it is clear of this vehicle's
  // own collider, plus a margin. A vehicle puts its bounding box into the
  // world so collision and raycasts treat it as solid, which is exactly what
  // a rocket leaving its own wing pylon runs into: it spawns inside the box
  // and detonates on the frame it is created. The projectile is
  // server-simulated and the server knows nothing about vehicles, so the fix
  // has to be to launch from outside the box in the first place.
  Vector3 ClearOfHull(int index, Vector3 from, Vector3 dir,
                      float margin = 14.0f) const;

  // `skip` is the vehicle the camera is sitting inside; drawing it would put
  // the far side of its own bodywork across the view.
  void Draw(const Assets& assets, Vector3 camPos, int skip = -1) const;

  // obj_car's own shell, rebuilt primitive for primitive from its create
  // event and wearing GTJ3D's car skin. This is the stand-in when a vehicle's
  // .glb is missing -- it is the car GTJ3D actually drew, which beats the
  // hand-stacked boxes that used to fill in. `interior` picks the taller
  // cabin and frame 1 of the skin, the pair obj_car swapped to once you were
  // at the wheel.
  //
  // True when it drew something; false means the skins are not staged.
  // `turning` is obj_player.turning -- 0 straight, 1 left, 2 right -- and
  // picks the steering wheel frame, which is only drawn for the interior.
  bool DrawGtjShell(const Assets& assets, int index, Vector3 camPos,
                    bool interior, int turning = 0) const;
  // Whether this vehicle has a GTJ3D shell at all -- the saloon and the SWAT
  // van are obj_car's own two variants; the tank, gunship and supercar are
  // not and have no equivalent.
  static bool HasGtjShell(int kind);

  std::vector<Vehicle>& list() { return vehicles_; }
  const std::vector<Vehicle>& list() const { return vehicles_; }
  int count() const { return static_cast<int>(vehicles_.size()); }

 private:
  void PushColliders(World& world);
  // The tank's turret, drawn over the hull at its own heading.
  void DrawTurret(const Vehicle& v, Color tint) const;
  // One step of a hostile tank's or gunship's brain.
  void DriveHostile(World& world, size_t index);

  std::vector<Vehicle> vehicles_;
  // accel/maxspeed are rolled once per vehicle, exactly as GTJ3D did in the
  // car's create event, so each one handles slightly differently.
  std::vector<float> accel_, maxSpeed_;
  // Vehicle index for each collider pushed into the world, in push order.
  std::vector<int> colliderVeh_;

  Vector3 threat_{};
  bool haveThreat_ = false;
};

// The oriented bounding box of a vehicle, in world space.
void VehicleBounds(const Vehicle& v, Vector3* outMin, Vector3* outMax);

}  // namespace kaj
