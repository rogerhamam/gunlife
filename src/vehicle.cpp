#include "vehicle.h"

#include <algorithm>
#include <cmath>

#include "assets.h"
#include "raymath.h"
#include "rlgl.h"

namespace kaj {
namespace {

float Fr01() { return static_cast<float>(GetRandomValue(0, 10000)) / 10000.0f; }

float RandBetween(float a, float b) { return a + (b - a) * Fr01(); }

// GTJ3D's obj_car measurements, verbatim, plus a tank cut to the same pattern.
const VehicleDef kDefs[] = {
    // saloon: length 58, width 32, model_height 7, front_length 25, height 20
    // seatHeight is GTJ3D's driving eye height minus our own eye offset.
    // obj_player put the camera at `z + height + 2` in a car and
    // `z + height + 15` in the SWAT van, with height = 14, and
    // LocalPlayer::eyePos adds kPlayerEye (15) on top of what SeatPos
    // returns -- so 14+2-15 = 1 and 14+15-15 = 14 reproduce those exactly.
    // The eye sits inside obj_car's own shell, which is what you are in while
    // driving. For the tank it is turret height, where a commander's head is.
    {"Saloon", 58.0f, 32.0f, 7.0f, 25.0f, 20.0f,
     0.05f, 3.0f, 0.1f, 4.0f,
     0.005f, 0.02f, 10.0f, 14.0f,
     100.0f, 1.0f, 32.0f},
    // SWAT van: length 58, width 32, model_height 18, front_length 5, height 30
    {"SWAT Van", 58.0f, 32.0f, 18.0f, 5.0f, 30.0f,
     0.05f, 2.4f, 0.09f, 3.5f,
     0.004f, 0.014f, 8.0f, 11.0f,
     160.0f, 14.0f, 34.0f},
    // Tank: heavier, slower, turns on the spot far worse, takes a beating.
    {"Tank", 96.0f, 52.0f, 22.0f, 12.0f, 46.0f,
     0.04f, 1.5f, 0.07f, 2.5f,
     0.0025f, 0.006f, 5.0f, 7.0f,
     600.0f, 34.0f, 52.0f},
    // Supercar: GTJ3D's saloon numbers taken to the top of their range and
    // past it. Quick and grippy, but there is almost nothing to it.
    {"Supercar", 54.0f, 30.0f, 5.0f, 26.0f, 17.0f,
     0.06f, 3.6f, 0.14f, 4.0f,
     0.022f, 0.034f, 17.0f, 20.0f,
     70.0f, 0.0f, 30.0f},
    // Gunship. GTJ3D's helicopter had life 50 and a hover ceiling of 120; it
     // is tougher here because you fly it yourself and the ground shoots back.
    {"Gunship", 130.0f, 56.0f, 30.0f, 20.0f, 54.0f,
     0.04f, 2.2f, 0.12f, 6.0f,
     0.010f, 0.016f, 14.0f, 18.0f,
     260.0f, 6.0f, 64.0f},
};

// GTJ3D's obj_enemy helicopter block: it never exceeded 1 unit of climb per
// step. Its fixed 120-unit hover ceiling is gone -- right for something the
// AI flies itself to, wrong for something a player is holding a collective
// on, which should hold whatever height it is put at.
constexpr float kHeliClimbAccel = 0.01f;
constexpr float kHeliMaxClimb = 1.0f;

// ------------------------------------------------------- GTJ3D's obj_car shell
//
// Every number here is read straight out of obj_car's Create event, and the
// geometry below is its `interior_model` build, primitive for primitive. The
// interior model is the same shell as the exterior with one change --
// box_height goes from 6 to 12, lifting the cabin roof and stretching the
// side panels -- and it is drawn with frame 1 of the car's skin instead of
// frame 0. That pair of changes is the whole trick: from the driver's seat
// you are inside a taller box wearing its inside face.
struct GtjCar {
  float length, width, modelHeight, frontLength;
  float boxX1, boxX2, boxThickness, boxPadding;
  float bonnetX1, bonnetX2;
  float frontZ, frontWidth;
  float steerDrop;    // obj_car's `wheel_d`: -8 in the van, 0 in a car
};

GtjCar GtjCarSpec(int kind) {
  const bool swat = (kind == VEH_VAN);
  GtjCar s{};
  s.length = 58.0f;
  s.width = 32.0f;
  s.modelHeight = swat ? 18.0f : 7.0f;
  s.frontLength = swat ? 5.0f : 25.0f;
  s.boxX1 = 12.0f;
  s.boxX2 = s.length - 16.0f;            // 42
  s.boxThickness = 2.0f;
  s.boxPadding = 2.0f;
  s.bonnetX1 = swat ? 1.0f : 6.0f;
  s.bonnetX2 = s.boxX2 + (swat ? 5.0f : 15.0f);
  s.frontZ = 3.0f;
  s.frontWidth = 8.0f;
  s.steerDrop = swat ? -8.0f : 0.0f;
  return s;
}

// obj_car's interior cabin height. The exterior shell uses 6.
constexpr float kGtjInteriorBoxHeight = 12.0f;
constexpr float kGtjExteriorBoxHeight = 6.0f;

// GTJ3D model space -> world. The draw event offsets the model by
// (-length/2, -width/2) before rotating, so obj_car's origin is the middle of
// the *cabin* box with the bonnet running on ahead of it. Our chassis origin
// is the middle of the whole footprint, which is front_length/2 further
// forward -- hence the extra shift. GTJ's (x, y, z) is our (x, z, y).
Vector3 GtjToWorld(const GtjCar& s, const Vehicle& v, float mx, float my,
                   float mz, float lift) {
  const float lx = mx - (s.length + s.frontLength) * 0.5f;
  const float lz = my - s.width * 0.5f;
  const float rad = DegToRadF(v.dirDeg);
  const float c = cosf(rad), sn = sinf(rad);
  return Vector3{v.pos.x + lx * c + lz * sn, v.pos.y + mz + lift,
                 v.pos.z - lx * sn + lz * c};
}

// One triangle, with a normal turned toward the camera. GTJ3D ran the whole
// game with d3d_set_culling(false) (obj_player's Create), so its models are
// not consistently wound and several of these strips are visible from both
// sides -- flipping the normal to face the viewer is what keeps them lit
// rather than half of them coming out black.
void EmitTri(Vector3 a, Vector3 b, Vector3 c, Vector2 ua, Vector2 ub,
             Vector2 uc, Vector3 camPos, Color tint) {
  Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
  const float len = Vector3Length(n);
  if (len < 1e-6f) return;                       // degenerate strip step
  n = Vector3Scale(n, 1.0f / len);
  if (Vector3DotProduct(n, Vector3Subtract(camPos, a)) < 0.0f)
    n = Vector3Negate(n);
  rlNormal3f(n.x, n.y, n.z);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);
  rlTexCoord2f(ua.x, ua.y); rlVertex3f(a.x, a.y, a.z);
  rlTexCoord2f(ub.x, ub.y); rlVertex3f(b.x, b.y, b.z);
  rlTexCoord2f(uc.x, uc.y); rlVertex3f(c.x, c.y, c.z);
}

// d3d_model_primitive_begin(pr_trianglestrip): vertices 0,1,2 then 1,2,3 and
// so on, which is exactly how GameMaker fed these to the card. `first` and
// `count` select a run of the strip's triangles, which is how the panels that
// are really windows get separated from the pillars sharing their strip.
void EmitStrip(const Vector3* p, const Vector2* uv, int n, Vector3 camPos,
               Color tint, int first = 0, int count = 1 << 20) {
  const int tris = n - 2;
  const int end = (count > tris - first) ? tris : first + count;
  for (int i = first; i < end; ++i)
    EmitTri(p[i], p[i + 1], p[i + 2], uv[i], uv[i + 1], uv[i + 2], camPos,
            tint);
}

// d3d_model_block: a six-sided box, each face carrying the texture once.
void EmitBlock(const GtjCar& s, const Vehicle& v, float x1, float y1, float z1,
               float x2, float y2, float z2, float lift, Vector3 camPos,
               Color tint) {
  Vector3 c[8];
  int i = 0;
  for (int zi = 0; zi < 2; ++zi)
    for (int yi = 0; yi < 2; ++yi)
      for (int xi = 0; xi < 2; ++xi)
        c[i++] = GtjToWorld(s, v, xi ? x2 : x1, yi ? y2 : y1, zi ? z2 : z1,
                            lift);
  // Corner indices are (z<<2)|(y<<1)|x.
  static const int kFaces[6][4] = {
      {0, 1, 3, 2},   // bottom  z1
      {4, 5, 7, 6},   // top     z2
      {0, 1, 5, 4},   // y1 side
      {2, 3, 7, 6},   // y2 side
      {0, 2, 6, 4},   // x1 end
      {1, 3, 7, 5},   // x2 end
  };
  static const Vector2 kUv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (const auto& f : kFaces) {
    EmitTri(c[f[0]], c[f[1]], c[f[2]], kUv[0], kUv[1], kUv[2], camPos, tint);
    EmitTri(c[f[0]], c[f[2]], c[f[3]], kUv[0], kUv[2], kUv[3], camPos, tint);
  }
}

}  // namespace

const VehicleDef& VehicleInfo(int kind) {
  if (kind < 0 || kind >= VEH_KINDS) kind = 0;
  return kDefs[kind];
}

void VehicleBounds(const Vehicle& v, Vector3* outMin, Vector3* outMax) {
  const VehicleDef& d = VehicleInfo(v.kind);
  // The chassis is an oriented box; collision uses the axis-aligned box that
  // contains it, which is close enough at these sizes and keeps every existing
  // collision, ground-height and raycast path working untouched.
  const float rad = DegToRadF(v.dirDeg);
  const float c = fabsf(cosf(rad)), s = fabsf(sinf(rad));
  const float halfL = (d.length + d.frontLength) * 0.5f;
  const float halfW = d.width * 0.5f;
  const float ex = halfL * c + halfW * s;
  const float ez = halfL * s + halfW * c;
  *outMin = Vector3{v.pos.x - ex, v.pos.y, v.pos.z - ez};
  *outMax = Vector3{v.pos.x + ex, v.pos.y + d.height, v.pos.z + ez};
}

void VehicleSystem::Reset(const World& world) {
  vehicles_.clear();
  accel_.clear();
  maxSpeed_.clear();
  colliderVeh_.clear();
  int buried = 0;
  for (const VehicleSpawn& s : world.vehicleSpawns()) {
    // A `car` line is a pair of numbers somebody typed, and a map edit that
    // moves a building over one leaves a car embedded in a wall: undrivable,
    // half inside the brickwork, and solid enough to block the pavement. Any
    // spawn whose chassis does not fit where it was put is dropped rather
    // than left there. Traffic is exempt -- it is authored as a route and its
    // start point is only the first waypoint.
    if (!s.ai) {
      const VehicleDef& def = VehicleInfo(s.kind);
      const float r = fmaxf(def.width, def.length + def.frontLength) * 0.5f;
      Vector3 feet = s.pos;
      feet.y = world.GroundHeight(feet.x, feet.z, r * 0.6f, feet.y + 24.0f);
      if (!world.IsFree(feet, r * 0.72f, def.height)) {
        TraceLog(LOG_WARNING,
                 "MAP: %s at (%.0f, %.0f) is inside geometry, removed",
                 def.name, s.pos.x, s.pos.z);
        ++buried;
        continue;
      }
    }
    Vehicle v;
    v.kind = s.kind;
    v.paint = s.paint;
    v.pos = s.pos;
    v.dirDeg = s.yawDeg;
    v.turretDeg = s.yawDeg;
    v.ai = s.ai;
    v.wayA = s.wayA;
    v.wayB = s.wayB;
    const VehicleDef& d = VehicleInfo(v.kind);
    v.life = d.maxLife;
    vehicles_.push_back(v);
    // Rolled once, in the create event, exactly as GTJ3D did -- so no two cars
    // on the map pull away at quite the same rate.
    accel_.push_back(RandBetween(d.accelMin, d.accelMax));
    maxSpeed_.push_back(
        static_cast<float>(GetRandomValue((int)d.speedMin, (int)d.speedMax)));
  }
  if (buried > 0)
    TraceLog(LOG_INFO, "MAP: %d vehicle spawn(s) removed for being inside "
                       "geometry, %d placed", buried, count());
}

void VehicleSystem::Tick(World& world, int driven, const DriveInput& in) {
  for (size_t i = 0; i < vehicles_.size(); ++i) {
    Vehicle& v = vehicles_[i];
    const VehicleDef& d = VehicleInfo(v.kind);
    v.crashImpulse = 0.0f;

    const bool mine = (static_cast<int>(i) == driven);
    // Only the vehicle under local control is simulated here. Everything else
    // is either parked or being driven by someone whose state arrives over the
    // network, so integrating it locally would just fight the snapshots.
    if (!mine) {
      if (v.hostile && v.driver < 0 && v.life > 0.0f) {
        DriveHostile(world, i);
        v.wheelSpin += v.speed * 2.0f;
        continue;
      }
      if (v.ai && v.driver < 0) {
        // Traffic: run between two waypoints, turn around at each end. Enough
        // to give you something moving to drive alongside and crash into.
        const Vector3 target = v.towardB ? v.wayB : v.wayA;
        const float dx = target.x - v.pos.x, dz = target.z - v.pos.z;
        const float dist = sqrtf(dx * dx + dz * dz);
        if (dist < 60.0f) v.towardB = !v.towardB;
        const float want = atan2f(-dz, dx) * RAD2DEG;
        // Same 3 degrees a step the player gets, so traffic corners the way a
        // driven car does rather than pivoting on the spot.
        float diff = fmodf(want - v.dirDeg + 540.0f, 360.0f) - 180.0f;
        const float turn = fminf(fabsf(diff), d.turning);
        v.dirDeg += diff < 0.0f ? -turn : turn;
        // Cruise at two thirds, using the same approach curve as the player's.
        const float cruise = maxSpeed_[i] * 0.66f;
        v.speed += (cruise - v.speed) * (accel_[i] + 0.02f);
        const float rad = DegToRadF(v.dirDeg);
        Vector3 next{v.pos.x + cosf(rad) * v.speed, v.pos.y,
                     v.pos.z - sinf(rad) * v.speed};
        next.y = world.GroundHeight(next.x, next.z, d.width * 0.4f,
                                    v.pos.y + 24.0f);
        v.pos = next;
      } else if (v.driver < 0) {
        v.speed = 0.0f;
        v.engineOn = false;
      }
      // Wheels still turn for a remote car, driven by its reported speed.
      v.wheelSpin += v.speed * 2.0f;
      continue;
    }

    // --- helicopter ---------------------------------------------------------
    if (v.kind == VEH_HELI) {
      // Rotor: spools up over about two seconds, then holds flight rpm. It
      // has to be turning before there is any lift.
      v.spool = fminf(1.0f, v.spool + 1.0f / 120.0f);
      v.rotor = fmodf(v.rotor + 6.0f + v.spool * 34.0f, 360.0f);

      const float ground = world.GroundHeight(v.pos.x, v.pos.z, d.width * 0.4f,
                                              v.pos.y + 30.0f);
      const float alt = v.pos.y - ground;

      // The mouse yaws the airframe -- the nose goes where you look. Its
      // pitch is look only: tying the machine's attitude to where you were
      // looking meant you could not glance down without diving, and could not
      // fly forward without staring at the floor.
      if (in.haveAir) v.dirDeg = in.heading;

      // --- cyclic, on W/S ---------------------------------------------------
      // Tipping the rotor disc is what moves a helicopter, so W puts the nose
      // down and S brings it up. `pitchDeg` is positive nose-down: the body is
      // drawn after a 180 degree yawFix, so its nose is model -X and a
      // positive rotation about +Z tips that end toward the ground.
      constexpr float kNoseDown = 26.0f;   // full forward cyclic
      constexpr float kNoseUp = -20.0f;    // full aft cyclic, which also brakes
      float wantPitch = 0.0f;
      if (in.fwd) wantPitch = kNoseDown;
      else if (in.back) wantPitch = kNoseUp;
      // Eased, not snapped: the disc takes a moment to tilt, and that lag is
      // most of what makes it feel like an aircraft rather than a camera.
      v.pitchDeg += (wantPitch - v.pitchDeg) * 0.07f;

      // Bank on A/D, which is what slides it sideways.
      float slide = 0.0f;
      if (in.left) { slide = -1.0f; v.rollDeg += (-20.0f - v.rollDeg) * 0.09f; }
      else if (in.right) { slide = 1.0f; v.rollDeg += (20.0f - v.rollDeg) * 0.09f; }
      else v.rollDeg *= 0.92f;

      // --- collective, on Space / Ctrl -------------------------------------
      float climb = 0.0f;
      // Climb authority falls away with height, so there is a service ceiling
      // rather than an open door to orbit.
      const float lift = Clampf(1.0f - (alt - 700.0f) / 500.0f, 0.0f, 1.0f);
      if (in.climb) climb += kHeliClimbAccel * 3.6f * lift;
      if (in.sink) climb -= kHeliClimbAccel * 3.6f;
      if (fabsf(climb) < 1e-5f) {
        // Hands off the collective, it holds the height it is at. GTJ3D's
        // climbed to a fixed ceiling on its own, which is right for something
        // the AI flies and wrong for something you are holding.
        v.vy *= 0.90f;
        // ...except off the deck, where it lifts itself clear rather than
        // grinding along the ground the moment the rotor is up to speed.
        if (alt < 26.0f) climb += kHeliClimbAccel * 1.4f;
      }
      v.vy = Clampf((v.vy + climb) * 0.985f, -kHeliMaxClimb * 2.2f,
                    kHeliMaxClimb * 2.2f);
      v.vy *= v.spool;                            // no lift without rpm

      // Nose-down converts into forward speed; nose-up brakes and backs up.
      const float want = v.pitchDeg / 45.0f * maxSpeed_[i];
      v.speed += (want - v.speed) * 0.05f;

      // Thrust follows the airframe, not the compass. A rotor pushes at right
      // angles to its disc, so tipping the nose down drives it forward *and*
      // down, and pulling the nose up climbs as it brakes. The old version
      // took the thrust along a flat heading vector and used the pitch only
      // to scale it, which meant the nose could be pointing anywhere and the
      // machine still slid along level ground.
      const float rad = DegToRadF(v.dirDeg);
      const float p = DegToRadF(v.pitchDeg);      // positive = nose down
      const Vector3 fwd{cosf(rad) * cosf(p), -sinf(p), -sinf(rad) * cosf(p)};
      const Vector3 rt = FlatRight(v.dirDeg);
      // A banked helicopter slips toward the low wing. The strafe keys set
      // the bank, so both act the same way and the drift reads as the turn.
      const float bank = sinf(DegToRadF(v.rollDeg));
      const float lateral = slide * 3.4f + bank * fabsf(v.speed) * 0.35f;
      Vector3 next = v.pos;
      next.x += fwd.x * v.speed + rt.x * lateral;
      next.z += fwd.z * v.speed + rt.z * lateral;
      // The collective and the vertical component of the thrust stack.
      next.y += v.vy + fwd.y * v.speed;

      // Nothing fancy for collision in the air: refuse to enter solid, and
      // never sink through the ground.
      //
      // Take our own collider out of the world before testing. A vehicle
      // pushes its bounding box into the world every tick so that collision
      // and raycasts treat it as solid -- and the gunship was then testing
      // itself against that box, failing every single tick, and having its
      // x and z reverted. Vertical movement is not gated by this test, which
      // is exactly why the machine could climb and descend and do nothing
      // else. The car branch has always cleared them; this one never did.
      world.SetVehicleColliders({});
      const float g2 = world.GroundHeight(next.x, next.z, d.width * 0.4f,
                                          next.y + 30.0f);
      if (next.y < g2) { next.y = g2; if (v.vy < 0.0f) v.vy = 0.0f; }
      if (!world.IsFree(Vector3{next.x, next.y, next.z}, d.width * 0.42f,
                        d.height)) {
        // Clipped something: stop dead and take the knock.
        if (fabsf(v.speed) > 4.0f) {
          v.crashImpulse = fabsf(v.speed);
          v.life -= powf(fabsf(v.speed), 1.4f);
        }
        v.speed *= -0.2f;
        next.x = v.pos.x;
        next.z = v.pos.z;
      }
      v.pos = next;
      continue;
    }

    // --- GTJ3D obj_player's driving block, step for step -------------------
    if (in.fwd) {
      const float acc = accel_[i] + v.friction;
      v.speed += fabsf(maxSpeed_[i] - v.speed) * acc;
      if (v.speed > maxSpeed_[i]) v.speed = maxSpeed_[i];
      v.engineOn = true;
    }
    if (in.back) {
      const float acc = d.brake + v.friction;
      if (v.speed > 0.0f) v.speed -= fabsf(v.speed) * acc;
      else v.speed -= 0.5f;
      if (v.speed < -d.reverseSpeed) v.speed = -d.reverseSpeed;
    }
    // Steering only bites when the wheels are turning, which is why a parked
    // GTJ car cannot be spun on the spot. Tracks are the exception: a tank
    // pivots on the spot by running them in opposite directions, so its hull
    // always steers, and it only ever drives straight down its own heading.
    if (fabsf(v.speed) > 0.0f || v.kind == VEH_TANK) {
      // Reverse steers the way a real car does: the back of the car swings the
      // other way, so the heading change flips sign below zero speed.
      const float sign = (v.speed < 0.0f && v.kind != VEH_TANK) ? -1.0f : 1.0f;
      if (in.left)  { v.dirDeg += d.turning * sign; v.aimWheelAngle = 30.0f; }
      if (in.right) { v.dirDeg -= d.turning * sign; v.aimWheelAngle = -30.0f; }
    }

    v.friction = (in.fwd || in.left || in.right) ? 0.01f : 0.05f;
    // GameMaker's built-in `friction` bleeds speed off every step. That is the
    // only thing slowing a coasting GTJ car, so it has to be here too.
    if (v.speed > 0.0f) v.speed = fmaxf(0.0f, v.speed - v.friction);
    else if (v.speed < 0.0f) v.speed = fminf(0.0f, v.speed + v.friction);

    // Wheel cosmetics.
    v.wheelSpin += v.speed * 2.0f;
    if (v.aimWheelAngle < 0.0f) v.aimWheelAngle += 1.0f;
    if (v.aimWheelAngle > 0.0f) v.aimWheelAngle -= 1.0f;
    if (fabsf(v.aimWheelAngle) < 0.5f) v.aimWheelAngle = 0.0f;
    v.wheelAngle += (v.aimWheelAngle - v.wheelAngle) * 0.1f;

    // --- move and collide --------------------------------------------------
    const float rad = DegToRadF(v.dirDeg);
    const Vector3 step{cosf(rad) * v.speed, 0.0f, -sinf(rad) * v.speed};
    if (fabsf(v.speed) > 0.0001f) {
      const float rSelf = fmaxf(d.width, d.length + d.frontLength) * 0.5f;
      // Take our own collider out of the world before testing, or the vehicle
      // instantly collides with the box it is standing in.
      world.SetVehicleColliders({});
      bool hit = false;
      const Vector3 before = v.pos;
      Vector3 next = world.SlideMove(v.pos, step, rSelf * 0.72f, d.height, &hit);
      next.y = world.GroundHeight(next.x, next.z, rSelf * 0.6f, v.pos.y + 24.0f);
      v.pos = next;
      if (hit) {
        const float moved = Vector3Distance(before, next);
        // GTJ crashed at speed > 6: heavy damage, a big shake and the car
        // bounced back at 30% of what it was doing.
        if (fabsf(v.speed) > 6.0f && moved < fabsf(v.speed) * 0.6f) {
          v.crashImpulse = fabsf(v.speed);
          v.life -= powf(fabsf(v.speed), 1.5f);
          v.speed *= -0.3f;
        } else {
          v.speed *= 0.8f;
        }
      }
    }
  }

  PushColliders(world);
}

void VehicleSystem::PushColliders(World& world) {
  std::vector<Brush> boxes;
  boxes.reserve(vehicles_.size());
  colliderVeh_.clear();
  colliderVeh_.reserve(vehicles_.size());
  for (size_t i = 0; i < vehicles_.size(); ++i) {
    const Vehicle& v = vehicles_[i];
    if (v.life <= 0.0f) continue;
    Brush b;
    VehicleBounds(v, &b.min, &b.max);
    b.tex = "metal";
    b.tile = 40.0f;
    b.tint = v.paint;
    b.invisible = true;   // the vehicle draws itself, oriented
    boxes.push_back(b);
    colliderVeh_.push_back(static_cast<int>(i));
  }
  world.SetVehicleColliders(boxes);
}

bool VehicleSystem::DamageByBrush(const World& world, int brushIndex,
                                  float damage, int* outVehicle) {
  const int base = world.staticBrushCount();
  if (brushIndex < base) return false;
  const int slot = brushIndex - base;
  if (slot < 0 || slot >= static_cast<int>(colliderVeh_.size())) return false;
  const int vi = colliderVeh_[slot];
  if (vi < 0 || vi >= count()) return false;
  Vehicle& v = vehicles_[vi];
  if (v.life <= 0.0f) return false;
  v.life -= damage;
  if (outVehicle) *outVehicle = vi;
  return true;
}

int VehicleSystem::hostileCount() const {
  int n = 0;
  for (const Vehicle& v : vehicles_)
    if (v.hostile && v.life > 0.0f) ++n;
  return n;
}

void VehicleSystem::ClearHostiles() {
  for (size_t i = vehicles_.size(); i-- > 0;) {
    if (!vehicles_[i].hostile) continue;
    vehicles_.erase(vehicles_.begin() + i);
    accel_.erase(accel_.begin() + i);
    maxSpeed_.erase(maxSpeed_.begin() + i);
  }
  colliderVeh_.clear();
}

int VehicleSystem::SpawnHostile(const World& world, int kind, Vector3 at,
                                Color paint) {
  const VehicleDef& d = VehicleInfo(kind);
  // A tank needs a lot of clear ground; a gunship only needs sky, so it is
  // dropped straight in at altitude and never has to find a road.
  Vector3 pos = at;
  if (kind == VEH_HELI) {
    pos.y = world.GroundHeight(pos.x, pos.z, d.width * 0.4f, 1e5f) + 260.0f;
  } else {
    const float r = fmaxf(d.width, d.length + d.frontLength) * 0.5f;
    const Vector3 clear = world.FindClearPoint(Vector3{at.x, 0.0f, at.z},
                                               r * 0.75f, d.height);
    // FindClearPoint gives up and returns the point it was handed when the
    // ground is solid for miles; refusing to spawn is better than dropping a
    // tank inside a building.
    if (!world.IsFree(clear, r * 0.72f, d.height)) return -1;
    pos = clear;
  }

  Vehicle v;
  v.kind = kind;
  v.paint = paint;
  v.pos = pos;
  v.hostile = true;
  v.life = d.maxLife;
  // Facing the middle of the map is as good a start as any; the brain turns
  // it toward the player on its first tick.
  v.dirDeg = 0.0f;
  v.turretDeg = 0.0f;
  v.spool = kind == VEH_HELI ? 1.0f : 0.0f;
  // A van rolls up with a full squad aboard.
  if (kind == VEH_VAN) v.occupants = kSwatSquad;
  vehicles_.push_back(v);
  accel_.push_back(RandBetween(d.accelMin, d.accelMax));
  maxSpeed_.push_back(
      static_cast<float>(GetRandomValue((int)d.speedMin, (int)d.speedMax)));
  return count() - 1;
}

// A hostile tank or gunship, flown/driven through the same handling model the
// player gets rather than slid along a path: it accelerates, corners and
// climbs with the same constants, so it behaves like something you could take
// the controls of.
void VehicleSystem::DriveHostile(World& world, size_t index) {
  Vehicle& v = vehicles_[index];
  const VehicleDef& d = VehicleInfo(v.kind);
  if (v.fireCooldown > 0) --v.fireCooldown;
  v.firedWeapon = -1;
  if (!haveThreat_) return;

  const Vector3 to = Vector3Subtract(threat_, v.pos);
  const float flat = sqrtf(to.x * to.x + to.z * to.z);
  const float wantYaw = atan2f(-to.z, to.x) * RAD2DEG;

  if (v.kind == VEH_HELI) {
    // Stand off at height and work the target over. It holds a ring rather
    // than flying straight at you, so it stays a problem instead of crashing
    // into the block you are standing behind.
    constexpr float kStandoff = 620.0f;
    constexpr float kCeiling = 300.0f;
    v.spool = fminf(1.0f, v.spool + 1.0f / 120.0f);
    v.rotor = fmodf(v.rotor + 6.0f + v.spool * 34.0f, 360.0f);

    float diff = fmodf(wantYaw - v.dirDeg + 540.0f, 360.0f) - 180.0f;
    v.dirDeg += Clampf(diff, -2.2f, 2.2f);

    const float ground =
        world.GroundHeight(v.pos.x, v.pos.z, d.width * 0.4f, 1e5f);
    const float alt = v.pos.y - ground;
    v.vy += Clampf((kCeiling - alt) * 0.004f, -0.5f, 0.5f);
    v.vy = Clampf(v.vy * 0.94f, -1.6f, 1.6f);

    // Nose in when it is too far out, back off when it is too close.
    const float closing = Clampf((flat - kStandoff) / 400.0f, -1.0f, 1.0f);
    v.pitchDeg += (closing * 22.0f - v.pitchDeg) * 0.05f;
    const float want = v.pitchDeg / 45.0f * maxSpeed_[index];
    v.speed += (want - v.speed) * 0.04f;
    // A slow orbit so it is never a stationary target.
    const float orbit = 1.5f;
    v.rollDeg += (-14.0f - v.rollDeg) * 0.04f;

    const float rad = DegToRadF(v.dirDeg);
    const Vector3 fwd{cosf(rad), 0.0f, -sinf(rad)};
    const Vector3 rt{-sinf(rad), 0.0f, -cosf(rad)};
    Vector3 next = v.pos;
    next.x += fwd.x * v.speed + rt.x * orbit;
    next.z += fwd.z * v.speed + rt.z * orbit;
    next.y += v.vy;
    const float g2 = world.GroundHeight(next.x, next.z, d.width * 0.4f, 1e5f);
    if (next.y < g2 + 60.0f) { next.y = g2 + 60.0f; v.vy = fmaxf(v.vy, 0.0f); }
    v.pos = next;

    // Minigun: bursts of a dozen, only with a clear line down to the target.
    // Every third pause it puts a rocket in instead, so the thing overhead is
    // something you have to get out from under rather than a noise.
    const Vector3 muzzle{v.pos.x, v.pos.y - 6.0f, v.pos.z};
    if (v.fireCooldown == 0 && flat < 1400.0f &&
        world.LineOfSight(muzzle, threat_)) {
      if (v.burst <= 0) {
        ++v.salvo;
        if (v.salvo % 3 == 0) {
          // A rocket, then a long reload before the guns come back.
          v.fireCooldown = 260;
          v.firedWeapon = 2;
          v.fireFrom = muzzle;
          // Deliberately imperfect, like the tank's -- it lands near you.
          v.fireAt = Vector3Add(threat_, Vector3{RandBetween(-40.0f, 40.0f),
                                                 0.0f,
                                                 RandBetween(-40.0f, 40.0f)});
          return;
        }
        v.burst = 12;
      }
      --v.burst;
      v.fireCooldown = v.burst > 0 ? 4 : 150;   // burst, then a long pause
      v.firedWeapon = 1;
      v.fireFrom = muzzle;
      v.fireAt = threat_;
    }
    return;
  }

  // --- SWAT van -----------------------------------------------------------
  // obj_car's swat block: drive at the player carrying a squad, and once you
  // are close enough stop dead and put them in the street. GTJ3D used 180
  // units and `occupants = irandom_range(4, 6)`; this one carries eight and
  // stops a little further out so the squad has room to fan out rather than
  // spawning on top of you.
  //
  // It is a personnel carrier and nothing else: it never sets `firedWeapon`,
  // so it has no gun of any kind. Everything it brings to the fight walks out
  // of the back and shoots on its own two feet.
  if (v.kind == VEH_VAN) {
    constexpr float kDropRange = 340.0f;
    float diffV = fmodf(wantYaw - v.dirDeg + 540.0f, 360.0f) - 180.0f;
    if (!v.deployed) {
      const float turn = fminf(fabsf(diffV), d.turning);
      v.dirDeg += diffV < 0.0f ? -turn : turn;
      // Full pelt until it is in range, then everything on the brakes.
      const float want = flat > kDropRange ? maxSpeed_[index] : 0.0f;
      v.speed += (want - v.speed) * (accel_[index] + 0.06f);
      if (want == 0.0f) v.speed *= 0.82f;
      if (flat <= kDropRange && fabsf(v.speed) < 1.2f) {
        v.speed = 0.0f;
        v.deployed = true;
        v.dropOff = v.occupants;      // Game reads this and spawns the squad
        v.occupants = 0;
      }
    } else {
      v.speed *= 0.7f;                // parked, with the squad out
    }

    if (fabsf(v.speed) > 0.0001f) {
      const float rad = DegToRadF(v.dirDeg);
      const Vector3 step{cosf(rad) * v.speed, 0.0f, -sinf(rad) * v.speed};
      const float rSelf = fmaxf(d.width, d.length + d.frontLength) * 0.5f;
      world.SetVehicleColliders({});
      bool hit = false;
      Vector3 next = world.SlideMove(v.pos, step, rSelf * 0.72f, d.height, &hit);
      next.y = world.GroundHeight(next.x, next.z, rSelf * 0.6f, v.pos.y + 24.0f);
      v.pos = next;
      if (hit) {
        v.speed *= 0.4f;
        v.dirDeg += RandBetween(-22.0f, 22.0f);
        // A van that has wedged itself somewhere still has a squad aboard.
        // Let them out rather than leaving them in a box against a wall.
        if (!v.deployed && flat < kDropRange * 2.4f) {
          v.deployed = true;
          v.dropOff = v.occupants;
          v.occupants = 0;
        }
      }
    }
    return;
  }

  // --- tank ---------------------------------------------------------------
  // Hull turns toward the target and closes to gun range; the turret tracks
  // independently, exactly as it does under the player.
  float diff = fmodf(wantYaw - v.dirDeg + 540.0f, 360.0f) - 180.0f;
  const float turn = fminf(fabsf(diff), d.turning);
  v.dirDeg += diff < 0.0f ? -turn : turn;
  float tdiff = fmodf(wantYaw - v.turretDeg + 540.0f, 360.0f) - 180.0f;
  v.turretDeg += Clampf(tdiff, -1.4f, 1.4f);

  // Close to about 700 units and hold there.
  const float cruise = flat > 700.0f ? maxSpeed_[index] * 0.8f
                                     : (flat < 380.0f ? -d.reverseSpeed * 0.6f
                                                      : 0.0f);
  v.speed += (cruise - v.speed) * (accel_[index] + 0.02f);
  if (fabsf(v.speed) > 0.0001f) {
    const float rad = DegToRadF(v.dirDeg);
    const Vector3 step{cosf(rad) * v.speed, 0.0f, -sinf(rad) * v.speed};
    const float rSelf = fmaxf(d.width, d.length + d.frontLength) * 0.5f;
    world.SetVehicleColliders({});
    bool hit = false;
    Vector3 next = world.SlideMove(v.pos, step, rSelf * 0.72f, d.height, &hit);
    next.y = world.GroundHeight(next.x, next.z, rSelf * 0.6f, v.pos.y + 24.0f);
    v.pos = next;
    if (hit) {
      v.speed *= 0.5f;
      // Nudge round the obstruction rather than grinding into it forever.
      v.dirDeg += RandBetween(-18.0f, 18.0f);
    }
  }

  // Main gun. Only when the turret is actually on target and the shot is
  // clear, so it cannot shell you through a building.
  const Vector3 muzzle = GunMuzzle(static_cast<int>(index), 4.0f);
  if (v.fireCooldown == 0 && fabsf(tdiff) < 4.0f && flat < 2400.0f &&
      world.LineOfSight(muzzle, threat_)) {
    v.fireCooldown = 190;
    v.firedWeapon = 0;
    v.fireFrom = muzzle;
    // Deliberately imperfect: it lands near you, not on you.
    v.fireAt = Vector3Add(threat_, Vector3{RandBetween(-34.0f, 34.0f), 0.0f,
                                           RandBetween(-34.0f, 34.0f)});
  }
}

int VehicleSystem::FindEnterable(Vector3 feet, float yawDeg) const {
  int best = -1;
  float bestDist = 1e9f;
  for (size_t i = 0; i < vehicles_.size(); ++i) {
    const Vehicle& v = vehicles_[i];
    if (v.driver >= 0 || v.life <= 0.0f) continue;
    const VehicleDef& d = VehicleInfo(v.kind);
    // GTJ tested the player's z against the car's own band, so you cannot
    // reach into a car from the roof of a building above it.
    if (feet.y > v.pos.y + d.height || feet.y < v.pos.y - 12.0f) continue;
    // Distance to the chassis, not to its centre. The centre test GTJ used
    // works there because its cars were not solid to the player; here the
    // vehicle's own collider stops you ~40 units short of the middle of a
    // saloon, which would put every car permanently out of reach.
    const float rad = DegToRadF(v.dirDeg);
    const float c = cosf(rad), s = sinf(rad);
    const float rx = feet.x - v.pos.x, rz = feet.z - v.pos.z;
    // World -> chassis frame (+X along its length).
    const float lx = rx * c - rz * s;
    const float lz = -(rx * s + rz * c);
    const float halfL = (d.length + d.frontLength) * 0.5f;
    const float halfW = d.width * 0.5f;
    const float ox = fmaxf(0.0f, fabsf(lx) - halfL);
    const float oz = fmaxf(0.0f, fabsf(lz) - halfW);
    const float dist = sqrtf(ox * ox + oz * oz);
    if (dist > 26.0f) continue;

    // ...and roughly in front of you, so you cannot reach through a wall or
    // grab a car you have your back to.
    const float toCar = atan2f(-(v.pos.z - feet.z), v.pos.x - feet.x) * RAD2DEG;
    const float diff = fmodf(toCar - yawDeg + 540.0f, 360.0f) - 180.0f;
    if (fabsf(diff) > 75.0f) continue;
    if (dist < bestDist) { bestDist = dist; best = static_cast<int>(i); }
  }
  return best;
}

Vector3 VehicleSystem::SeatPos(int index) const {
  if (index < 0 || index >= count()) return Vector3{};
  const Vehicle& v = vehicles_[index];
  const VehicleDef& d = VehicleInfo(v.kind);

  // --- gunship ------------------------------------------------------------
  // The pilot's head is a point on the airframe, so it has to be carried by
  // the airframe's *whole* attitude, not just its heading. With only the yaw
  // applied, pitching the nose over swung the cockpit through the lens while
  // the camera stayed put: the fuselage appeared to swim around a floating
  // viewpoint. Rotating the seat offset by pitch and roll as well welds the
  // eye into the cockpit, so the airframe is what stays still on screen.
  //
  // The offset is built around the *eye*, not the feet, and kPlayerEye is
  // taken back off at the end -- Game adds it again via LocalPlayer::eyePos.
  if (v.kind == VEH_HELI) {
    const float rad = DegToRadF(v.dirDeg);
    const Vector3 f{cosf(rad), 0.0f, -sinf(rad)};
    const Vector3 rt{sinf(rad), 0.0f, cosf(rad)};
    const float p = DegToRadF(v.pitchDeg);      // positive = nose down
    const float rl = DegToRadF(v.rollDeg);
    // Pitch tips forward toward the ground and lifts up along with it.
    const Vector3 fp = Vector3Add(Vector3Scale(f, cosf(p)),
                                  Vector3{0.0f, -sinf(p), 0.0f});
    const Vector3 upP = Vector3Add(Vector3Scale(f, sinf(p)),
                                   Vector3{0.0f, cosf(p), 0.0f});
    // Roll spins that up vector about the nose axis.
    const Vector3 up = Vector3Subtract(Vector3Scale(upP, cosf(rl)),
                                       Vector3Scale(rt, sinf(rl)));
    // Right up in the nose: from the middle of the fuselage you would be
    // looking at your own tail boom.
    const float fwd = (d.length + d.frontLength) * 0.31f;
    const float rise = d.seatHeight + kPlayerEye;
    const Vector3 eye = Vector3Add(v.pos, Vector3Add(Vector3Scale(fp, fwd),
                                                     Vector3Scale(up, rise)));
    return Vector3{eye.x, eye.y - kPlayerEye, eye.z};
  }

  // A car driver sits behind the engine, at the window line. A tank commander
  // is head-out of the cupola: above the turret roof and set back from the
  // ring, so the turret and the length of the barrel are both in shot.
  // The tank camera sits low over the turret and well back, so the eye runs
  // along the barrel to the crosshair rather than looking down onto it.
  //
  // A car's is GTJ3D's, exactly: obj_player put the camera at `car.x, car.y`,
  // which is the origin of obj_car's model -- the centre of the *cabin* box,
  // with the bonnet running on ahead of it. Our chassis origin is the middle
  // of the whole footprint, so that same point is half a bonnet further back.
  const float back = (v.kind == VEH_TANK) ? (d.length + d.frontLength) * 0.30f
                                          : d.frontLength * 0.5f;
  // ...and the cupola sits on the turret, which traverses with your aim.
  const float rad = DegToRadF(v.kind == VEH_TANK ? v.turretDeg : v.dirDeg);
  return Vector3{v.pos.x - cosf(rad) * back, v.pos.y + d.seatHeight,
                 v.pos.z + sinf(rad) * back};
}

Vector3 VehicleSystem::ClearOfHull(int index, Vector3 from, Vector3 dir,
                                   float margin) const {
  if (index < 0 || index >= count()) return from;
  Vector3 mn, mx;
  VehicleBounds(vehicles_[index], &mn, &mx);
  // Grow the box a little so a launch point that is merely touching the skin
  // still gets pushed clear.
  const float pad = 2.0f;
  mn = Vector3SubtractValue(mn, pad);
  mx = Vector3AddValue(mx, pad);

  // Slab test for where the ray leaves the box. If the start is outside
  // already, `exit` comes back at or below zero and nothing moves.
  const float o[3] = {from.x, from.y, from.z};
  const float d3[3] = {dir.x, dir.y, dir.z};
  const float lo[3] = {mn.x, mn.y, mn.z};
  const float hi[3] = {mx.x, mx.y, mx.z};
  float exit = 0.0f;
  for (int a = 0; a < 3; ++a) {
    if (fabsf(d3[a]) < 1e-6f) {
      // Parallel to this pair of planes: if we are outside them we can never
      // be inside the box at all.
      if (o[a] < lo[a] || o[a] > hi[a]) return from;
      continue;
    }
    const float t1 = (lo[a] - o[a]) / d3[a];
    const float t2 = (hi[a] - o[a]) / d3[a];
    const float far = fmaxf(t1, t2);
    if (far <= 0.0f) return from;              // box is behind us
    exit = fmaxf(exit, fminf(far, 4000.0f));
  }
  if (exit <= 0.0f) return from;
  return Vector3Add(from, Vector3Scale(dir, exit + margin));
}

Vector3 VehicleSystem::GunMuzzle(int index, float extraHeight) const {
  if (index < 0 || index >= count()) return Vector3{};
  const Vehicle& v = vehicles_[index];
  const VehicleDef& d = VehicleInfo(v.kind);
  // Out along the barrel from the turret ring.
  const float reach = (d.length + d.frontLength) * 0.52f;
  const float rad = DegToRadF(v.turretDeg);
  return Vector3{v.pos.x + cosf(rad) * reach,
                 v.pos.y + d.seatHeight + extraHeight,
                 v.pos.z - sinf(rad) * reach};
}

// The turret is drawn separately from the hull so it can traverse on its own.
// The glTF body has its turret baked into the same mesh, so this one is built
// a size larger and sits over the top of it -- from the cupola, which is where
// you view it from, what you see is this one swinging with your aim while the
// tracks carry on in whatever direction the hull is pointed.
void VehicleSystem::DrawTurret(const Vehicle& v, Color tint) const {
  const VehicleDef& d = VehicleInfo(v.kind);
  const float ring = d.modelHeight + 4.0f;      // turret ring, on the hull top
  const Color body{static_cast<unsigned char>(tint.r * 0.86f),
                   static_cast<unsigned char>(tint.g * 0.88f),
                   static_cast<unsigned char>(tint.b * 0.82f), 255};
  const Color steel{58, 60, 54, 255};

  rlPushMatrix();
  rlTranslatef(v.pos.x, v.pos.y + ring, v.pos.z);
  rlRotatef(v.turretDeg, 0.0f, 1.0f, 0.0f);

  const float tl = d.length * 0.38f, tw = d.width * 0.48f, thh = 11.0f;
  DrawCube(Vector3{0, thh * 0.5f, 0}, tl, thh, tw, body);
  DrawCubeWires(Vector3{0, thh * 0.5f, 0}, tl, thh, tw, steel);
  // Sloped mantlet at the front of the turret.
  DrawCube(Vector3{tl * 0.42f, thh * 0.44f, 0}, tl * 0.30f, thh * 0.72f,
           tw * 0.66f, body);
  // Commander's cupola, which is where the camera is.
  DrawCube(Vector3{-tl * 0.20f, thh + 3.0f, 0}, 13.0f, 6.0f, 13.0f, body);
  // The barrel: a long box rather than a cylinder, to stay in the game's
  // flat-shaded, hard-edged look.
  const float bl = d.length * 0.62f;
  DrawCube(Vector3{tl * 0.5f + bl * 0.5f, thh * 0.44f, 0}, bl, 6.5f, 6.5f,
           steel);
  DrawCube(Vector3{tl * 0.5f + bl - 5.0f, thh * 0.44f, 0}, 11.0f, 8.5f, 8.5f,
           steel);
  // Roof machine gun, offset to the commander's side.
  DrawCube(Vector3{4.0f, thh + 6.0f, tw * 0.34f}, 22.0f, 3.5f, 3.5f, steel);
  rlPopMatrix();
}

bool VehicleSystem::HasGtjShell(int kind) {
  return kind == VEH_SALOON || kind == VEH_VAN;
}

bool VehicleSystem::DrawGtjShell(const Assets& assets, int index,
                                 Vector3 camPos, bool interior,
                                 int turning) const {
  if (index < 0 || index >= count()) return false;
  const Vehicle& v = vehicles_[index];
  if (!HasGtjShell(v.kind) || !assets.haveCarSkins()) return false;

  const GtjCar s = GtjCarSpec(v.kind);
  const float bh = interior ? kGtjInteriorBoxHeight : kGtjExteriorBoxHeight;
  const float mh = s.modelHeight;
  const float bp = s.boxPadding, bt = s.boxThickness;
  const float bx1 = s.boxX1, bx2 = s.boxX2;
  const float nx1 = s.bonnetX1, nx2 = s.bonnetX2;
  const float L = s.length, W = s.width, FL = s.frontLength;
  const float fz = s.frontZ, fw = s.frontWidth;
  const float lift = 2.0f;              // draw event: translation(x, y, z + 2)

  // The paint colour picks whichever of GTJ3D's seven skins it is nearest.
  // A SWAT van is always black, exactly as obj_car forced car_color when
  // ID = "swat".
  const int skin = (v.kind == VEH_VAN) ? 6 : assets.NearestCarSkin(v.paint);
  const Texture2D& tex = assets.CarSkin(skin, interior);

  auto P = [&](float mx, float my, float mz) {
    return GtjToWorld(s, v, mx, my, mz, lift);
  };

  // The one place this is not obj_car verbatim. GTJ3D's shell has no glass in
  // it: the cabin sides are solid panels from model_height up to the roof and
  // the screens are solid diagonals across the same band -- so reproducing it
  // exactly seals the driver into a painted box with the camera, at model z
  // 14, in the middle of it. (GTJ3D got away with that because its `V` key
  // swung the camera 128 units out behind the car; here you drive from the
  // seat.) Every vertex below is still obj_car's, and the panels are still
  // exactly the panels it built -- the four that are really windows are drawn
  // as glass rather than as paint.
  const Color kGlass{150, 178, 200, 44};

  // ---- opaque bodywork ---------------------------------------------------
  // GTJ3D ran with culling off (obj_player's Create), and half these faces
  // are seen from behind, which is the whole point of an interior.
  rlDisableBackfaceCulling();
  rlSetTexture(tex.id);
  rlBegin(RL_TRIANGLES);

  // d3d_model_block(0, 0, 0, length, width, model_height)
  EmitBlock(s, v, 0, 0, 0, L, W, mh, lift, camPos, WHITE);
  // d3d_model_block(box_x1, padding, mh+bh, box_x2, width-padding, mh+bh+bt)
  EmitBlock(s, v, bx1, bp, mh + bh, bx2, W - bp, mh + bh + bt, lift, camPos,
            WHITE);

  // The rear and front strips each carry a pillar at either end (triangles 0
  // and 3) with the screen slung between them (triangles 1 and 2).
  const Vector3 rearP[6] = {P(bx1, W - bp, mh),      P(bx1, W - bp, mh + bh),
                            P(nx1, W - bp, mh),      P(bx1, bp, mh + bh),
                            P(nx1, bp, mh),          P(bx1, bp, mh)};
  const Vector2 rearUv[6] = {{0, 0}, {0, 0}, {1, 0}, {1, 0}, {0, 0}, {0, 0}};
  const Vector3 frontP[6] = {P(bx2, W - bp, mh),     P(bx2, W - bp, mh + bh),
                             P(nx2, W - bp, mh),     P(bx2, bp, mh + bh),
                             P(nx2, bp, mh),         P(bx2, bp, mh)};
  const Vector2 frontUv[6] = {{0, 0}, {0, 0}, {1, 0}, {1, 0}, {0, 0}, {0, 0}};
  EmitStrip(rearP, rearUv, 6, camPos, WHITE, 0, 1);
  EmitStrip(rearP, rearUv, 6, camPos, WHITE, 3, 1);
  EmitStrip(frontP, frontUv, 6, camPos, WHITE, 0, 1);
  EmitStrip(frontP, frontUv, 6, camPos, WHITE, 3, 1);

  {   // bonnet, upper
    const Vector3 p[5] = {P(L, 0, mh), P(L + FL, W * 0.5f + fw, fz),
                          P(L, W, mh), P(L + FL * 0.5f, W, fz),
                          P(L, W, fz)};
    const Vector2 uv[5] = {{0, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 0}};
    EmitStrip(p, uv, 5, camPos, WHITE);
  }
  {   // bonnet, lower
    const Vector3 p[6] = {P(L, 0, fz),  P(L + FL * 0.5f, 0, fz),
                          P(L, 0, mh),  P(L + FL, W * 0.5f - fw, fz),
                          P(L, W, mh),  P(L + FL, W * 0.5f + fw, fz)};
    const Vector2 uv[6] = {{0, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 0}, {1, 1}};
    EmitStrip(p, uv, 6, camPos, WHITE);
  }
  {   // front skirt, round the nose
    const Vector3 p[13] = {
        P(L, 0, fz),                      P(L, 0, 0),
        P(L + FL * 0.5f, 0, fz),          P(L + FL * 0.5f, 0, 0),
        P(L + FL, W * 0.5f - fw, fz),     P(L + FL, W * 0.5f - fw, 0),
        P(L + FL, W * 0.5f - fw, fz),     P(L + FL, W * 0.5f + fw, 0),
        P(L + FL, W * 0.5f + fw, fz),     P(L + FL * 0.5f, W, 0),
        P(L + FL * 0.5f, W, fz),          P(L, W, 0),
        P(L, W, fz)};
    const Vector2 uv[13] = {{0, 1}, {0, 1}, {0, 1}, {1, 0}, {1, 0}, {0, 0},
                            {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 1}, {1, 1},
                            {1, 1}};
    EmitStrip(p, uv, 13, camPos, WHITE);
  }
  rlEnd();

  // ---- glazing -----------------------------------------------------------
  // Windscreen, backlight and the two side windows: obj_car's own panels, on
  // its own vertices, drawn as glass. Untextured so they read as glass rather
  // than as a sheet of the car's paint held up in front of your face.
  // Depth writes off: the glass wraps right round the camera, and if it laid
  // down depth then every particle and tracer drawn in the later passes would
  // be rejected against it and the world would go dead outside the windows.
  rlDrawRenderBatchActive();
  rlDisableDepthMask();
  rlSetTexture(assets.white().id);
  rlBegin(RL_TRIANGLES);
  EmitStrip(rearP, rearUv, 6, camPos, kGlass, 1, 2);
  EmitStrip(frontP, frontUv, 6, camPos, kGlass, 1, 2);
  {   // cabin side, +y
    const Vector3 p[4] = {P(bx1, W - bp, mh), P(bx1, W - bp, mh + bh),
                          P(bx2, W - bp, mh), P(bx2, W - bp, mh + bh)};
    const Vector2 uv[4] = {{0, 0}, {1, 0}, {0, 0}, {1, 0}};
    EmitStrip(p, uv, 4, camPos, kGlass);
  }
  {   // cabin side, -y
    const Vector3 p[4] = {P(bx1, bp, mh), P(bx1, bp, mh + bh),
                          P(bx2, bp, mh), P(bx2, bp, mh + bh)};
    const Vector2 uv[4] = {{0, 0}, {1, 0}, {0, 0}, {1, 0}};
    EmitStrip(p, uv, 4, camPos, kGlass);
  }
  rlEnd();
  rlSetTexture(0);

  // ---- the steering wheel ------------------------------------------------
  // obj_car's draw event:
  //   rotation_y(-40), translation(1, 0, 0), rotation_z(direction),
  //   translation(x, y, z + model_height)
  //   d3d_draw_wall(0, -5, mh + 5 + wheel_d,  0, 5, mh + wheel_d, spr, 1, 1)
  // -- a 10 x 5 quad standing across the car, tilted back 40 degrees on its
  // column, one unit ahead of obj_car's origin and lifted by model_height on
  // top of the model's own height. Drawn in the world rather than as a HUD
  // overlay because that is where GTJ3D put it: look out of the side window
  // and the wheel stays with the car instead of following your eyes.
  //
  // Full white, and drawn after the glass rather than behind it: the wheel
  // and the hands on it are the one part of the driving view that is UI, and
  // running either through the cabin's tint changes the colour of the arms
  // with the car.
  const SpriteSheet& sw = assets.steeringWheel();
  if (interior && sw.valid()) {
    const float halfW = 5.0f;
    const float z0 = mh + s.steerDrop;            // bottom of the quad
    const float z1 = mh + 5.0f + s.steerDrop;     // top
    const float ca = cosf(DegToRadF(-40.0f)), sa = sinf(DegToRadF(-40.0f));
    // rotation_y: x' = x cos - z sin, z' = x sin + z cos, then +1 in x. The
    // model x is 0, so only the tilt of the column matters.
    auto WheelPt = [&](float y, float z) {
      const float mx = -z * sa + 1.0f;
      const float mz = z * ca;
      // These coordinates are relative to obj_car's origin, which is the
      // middle of the cabin box -- model x = length/2 -- not to the middle of
      // the whole footprint. ...and the base translation is z + model_height,
      // not the body's z + 2.
      return GtjToWorld(s, v, mx + L * 0.5f, y + W * 0.5f, mz, mh);
    };
    const Vector3 a = WheelPt(-halfW, z1), b = WheelPt(halfW, z1);
    const Vector3 c = WheelPt(halfW, z0), dd = WheelPt(-halfW, z0);
    const Texture2D& wt = sw.frame(turning);
    rlSetTexture(wt.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a),
                                    Vector3Subtract(dd, a));
    n = Vector3Normalize(n);
    if (Vector3DotProduct(n, Vector3Subtract(camPos, a)) < 0.0f)
      n = Vector3Negate(n);
    rlNormal3f(n.x, n.y, n.z);
    rlTexCoord2f(0, 0); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(0, 1); rlVertex3f(dd.x, dd.y, dd.z);
    rlTexCoord2f(1, 1); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(1, 0); rlVertex3f(b.x, b.y, b.z);
    rlEnd();
    rlSetTexture(0);
  }

  rlDrawRenderBatchActive();
  rlEnableDepthMask();
  rlEnableBackfaceCulling();
  return true;
}

// --------------------------------------------------------------------- draw
void VehicleSystem::Draw(const Assets& assets, Vector3 camPos, int skip) const {
  for (size_t vi = 0; vi < vehicles_.size(); ++vi) {
    if (static_cast<int>(vi) == skip) continue;
    const Vehicle& v = vehicles_[vi];
    if (v.life <= 0.0f) continue;
    const VehicleDef& d = VehicleInfo(v.kind);
    // Nothing to gain from drawing a car on the far side of the map, and the
    // glTF bodies are heavier than a brush.
    if (Vector3DistanceSqr(v.pos, camPos) > 2600.0f * 2600.0f) continue;

    const VehicleModel& m = assets.VehModel(v.kind);

    // A tank draws as two pieces cut from its own mesh: the hull, which turns
    // with the tracks, and the turret, which traverses on the ring under the
    // gunner's control. tools/split_tank.py does the cutting and prints the
    // ring's position, which is what the turret spins about here.
    // The gunship draws as a body plus a rotor spinning on the mast, both cut
    // out of its own mesh the same way the tank's turret was.
    if (v.kind == VEH_HELI && assets.heliSplit()) {
      const VehicleModel& body = assets.heliBody();
      const VehicleModel& rot = assets.heliRotor();
      auto mixh = [](unsigned char c) {
        return static_cast<unsigned char>(255.0f - (255.0f - c) * 0.92f);
      };
      const Color tint{mixh(v.paint.r), mixh(v.paint.g), mixh(v.paint.b), 255};
      rlPushMatrix();
      rlTranslatef(v.pos.x, v.pos.y, v.pos.z);
      rlRotatef(v.dirDeg + body.yawFix, 0.0f, 1.0f, 0.0f);
      // Attitude: nose tips into the direction of travel and it banks in the
      // turns, which is most of what sells a helicopter as flying.
      rlRotatef(v.pitchDeg, 0.0f, 0.0f, 1.0f);
      rlRotatef(v.rollDeg, 1.0f, 0.0f, 0.0f);
      rlScalef(body.scale, body.scale, body.scale);
      rlTranslatef(-body.centre.x, -body.centre.y, -body.centre.z);
      DrawModel(body.model, Vector3{0, 0, 0}, 1.0f, tint);
      rlTranslatef(assets.heliPivot().x, 0.0f, assets.heliPivot().z);
      rlRotatef(v.rotor, 0.0f, 1.0f, 0.0f);
      DrawModel(rot.model, Vector3{0, 0, 0}, 1.0f, tint);
      rlPopMatrix();
      continue;
    }

    if (v.kind == VEH_TANK && assets.tankSplit()) {
      const VehicleModel& hull = assets.tankHull();
      const VehicleModel& tur = assets.tankTurret();
      const Vector3 piv = assets.tankPivot();
      auto mix = [](unsigned char c) {
        return static_cast<unsigned char>(255.0f - (255.0f - c) * 0.92f);
      };
      const Color tint{mix(v.paint.r), mix(v.paint.g), mix(v.paint.b), 255};

      rlPushMatrix();
      rlTranslatef(v.pos.x, v.pos.y, v.pos.z);
      rlRotatef(v.dirDeg + hull.yawFix, 0.0f, 1.0f, 0.0f);
      rlScalef(hull.scale, hull.scale, hull.scale);
      rlTranslatef(-hull.centre.x, -hull.centre.y, -hull.centre.z);
      DrawModel(hull.model, Vector3{0, 0, 0}, 1.0f, tint);
      // Out to the ring, spin by however far the turret leads the hull, and
      // the turret's own geometry is already centred on that point.
      rlTranslatef(piv.x, 0.0f, piv.z);
      rlRotatef(v.turretDeg - v.dirDeg, 0.0f, 1.0f, 0.0f);
      DrawModel(tur.model, Vector3{0, 0, 0}, 1.0f, tint);
      rlPopMatrix();
      continue;
    }

    if (m.loaded) {
      rlPushMatrix();
      rlTranslatef(v.pos.x, v.pos.y, v.pos.z);
      rlRotatef(v.dirDeg + m.yawFix, 0.0f, 1.0f, 0.0f);
      rlScalef(m.scale, m.scale, m.scale);
      rlTranslatef(-m.centre.x, -m.centre.y, -m.centre.z);
      // The bodies carry their own baked texture, so the paint colour is a
      // multiply over it. Only a hair is taken off it -- pulling the tint
      // further back toward white washed the paint out into pastel, which
      // read as the texture not having loaded at all.
      auto mix = [](unsigned char c) {
        return static_cast<unsigned char>(255.0f - (255.0f - c) * 0.92f);
      };
      const Color tint{mix(v.paint.r), mix(v.paint.g), mix(v.paint.b), 255};
      DrawModel(m.model, Vector3{0, 0, 0}, 1.0f, tint);
      rlPopMatrix();
      if (v.kind == VEH_TANK) DrawTurret(v, tint);
      continue;
    }

    // The .glb is missing. For a saloon or a SWAT van there is a much better
    // stand-in than a stack of boxes: obj_car's own shell, rebuilt from its
    // create event and wearing GTJ3D's own skin. It is literally the car this
    // game grew out of.
    if (DrawGtjShell(assets, static_cast<int>(vi), camPos, /*interior=*/false))
      continue;

    // Nothing left but boxes -- the tank, the gunship or a car whose skins
    // were never staged.
    rlPushMatrix();
    rlTranslatef(v.pos.x, v.pos.y, v.pos.z);
    rlRotatef(v.dirDeg, 0.0f, 1.0f, 0.0f);
    const float halfL = (d.length + d.frontLength) * 0.5f;
    const Color glass{38, 46, 58, 255};
    DrawCube(Vector3{0, d.modelHeight * 0.5f + 4.0f, 0},
             d.length, d.modelHeight, d.width, v.paint);
    DrawCube(Vector3{halfL - 6.0f, d.modelHeight * 0.6f + 4.0f, 0},
             d.frontLength, d.modelHeight * 0.7f, d.width - 6.0f, v.paint);
    DrawCube(Vector3{-4.0f, d.modelHeight + 7.0f, 0},
             d.length * 0.5f, 6.0f, d.width - 4.0f, glass);
    rlPopMatrix();
  }
}

}  // namespace kaj
