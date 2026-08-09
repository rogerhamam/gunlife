#include "player.h"

#include "world.h"

namespace kaj {

void LocalPlayer::Reset(Vector3 spawnPos, float spawnYaw) {
  pos = spawnPos;
  yaw = spawnYaw;
  pitch = 0.0f;
  speed = 0.0f;
  vy = 0.0f;
  onGround = true;
  crouch = false;
  dead = false;
  health = kMaxHealth;
  armor = 0.0f;
  bobPhase = bobAmount = bobHeight = recoilPitch = landDip = 0.0f;
  zoomT = 0.0f;
  zoomed = false;
  crouchT = 0.0f;
  arsenal.ResetFull();
}

// Anything you can reach with your own jump is free; past that the damage
// goes with the square of the impact speed, which is proportional to the
// height fallen. kJumpSpeed is 2.6, so the safe limit is a shade above it to
// allow for a jump off a kerb on the way down.
float LocalPlayer::FallDamage(float impact) {
  // Impact speed relates to height as h = v^2 / (2 * kGravity), so with
  // kGravity 0.15 these work out at:
  //   kSafe  4.2  ->  ~59 units, a good five metres. Anything you would drop
  //                   off deliberately -- a kerb, a crate stack, one storey
  //                   -- costs nothing.
  //   kFatal 11.0 ->  ~400 units, ten storeys. Below that it hurts but you
  //                   walk away; the old 8.0 made a five-storey drop lethal
  //                   and a two-storey one expensive.
  constexpr float kSafe = 4.2f;
  constexpr float kFatal = 11.0f;
  if (impact <= kSafe) return 0.0f;
  const float t = (impact - kSafe) / (kFatal - kSafe);
  // Squared, so the tariff stays light through the middle of the range and
  // only bites near the bottom of a long drop. Capped a shade over a full
  // health bar rather than at 1.6 of one.
  return Clampf(t * t, 0.0f, 1.1f) * kMaxHealth;
}

float LocalPlayer::CurrentSpread() const {
  const WeaponDef& d = Weapon(arsenal.current);
  const float moveT = Clampf(fabsf(speed) / kRunSpeed, 0.0f, 1.0f);
  float s = d.spread + d.moveSpread * moveT;
  if (!onGround) s += 4.0f;             // jumping wrecks accuracy
  if (crouch) s *= 0.55f;
  if (d.canZoom && zoomT > 0.5f) s = d.zoomSpread + d.moveSpread * moveT * 0.25f;
  // One scale over the lot, so the airborne penalty and the scoped figure
  // tighten with everything else rather than being left behind.
  return s * kSpreadScale;
}

void LocalPlayer::ApplyRecoil(float degrees) {
  recoilPitch += degrees;
  if (recoilPitch > 12.0f) recoilPitch = 12.0f;
}

void LocalPlayer::Damage(float amount) {
  if (amount <= 0.0f) return;
  // Armour eats half of incoming damage until it is gone (GTJ carried an
  // armour pool alongside health; CS splits it the same way).
  if (armor > 0.0f) {
    const float absorbed = amount * 0.5f;
    const float used = absorbed < armor ? absorbed : armor;
    armor -= used;
    amount -= used;
  }
  health -= amount;
  if (health <= 0.0f) {
    health = 0.0f;
    dead = true;
  }
}

void LocalPlayer::Tick(const InputCommand& in, const World& world) {
  // -------------------------------------------------------------- view feel
  // Recoil decays back toward zero; the shot kick itself is applied elsewhere.
  recoilPitch *= 0.86f;
  if (fabsf(recoilPitch) < 0.01f) recoilPitch = 0.0f;
  landDip *= 0.80f;
  landImpact = 0.0f;

  zoomed = in.zoom && Weapon(arsenal.current).canZoom && !dead;
  const float zoomTarget = zoomed ? 1.0f : 0.0f;
  zoomT += (zoomTarget - zoomT) * 0.35f;

  arsenal.Tick();

  if (dead) {
    speed = 0.0f;
    return;
  }

  // ---------------------------------------------------------------- crouch
  // The intent flips instantly, but the capsule (and therefore the camera)
  // eases between standing and crouched over ~0.22 s in each direction.
  if (in.crouch) {
    crouch = true;
  } else if (crouch) {
    // Only stand back up if there is room overhead.
    const float ceil = world.CeilingHeight(pos.x, pos.z, kPlayerRadius, pos.y);
    if (pos.y + kPlayerHeight < ceil) crouch = false;
  }
  {
    const float target = crouch ? 1.0f : 0.0f;
    // Dropping is a touch quicker than rising, which is how it feels in CS.
    const float rate = (target > crouchT) ? 7.5f : 5.5f;
    const float step = rate * kTickDt;
    if (fabsf(target - crouchT) <= step) crouchT = target;
    else crouchT += (target > crouchT) ? step : -step;
  }

  // ---------------------------------------------------------- ground speed
  // GTJ: `speed` ramps by 0.4/tick to +/-maxspeed, GM friction removes
  // 0.2/tick, and A/D translate a flat 2 units per tick.
  float maxSpeed = in.sneak ? kSneakSpeed : kRunSpeed;
  if (crouch) maxSpeed *= kCrouchSpeedScale;

  if (in.moveForward > 0.01f) {
    speed = fminf(maxSpeed, speed + kAccel);
  } else if (in.moveForward < -0.01f) {
    speed = fmaxf(-maxSpeed, speed - kAccel);
  }
  if (speed > maxSpeed) speed = maxSpeed;
  if (speed < -maxSpeed) speed = -maxSpeed;

  float strafe = in.moveStrafe * kStrafeSpeed;
  if (in.sneak) strafe *= kSneakSpeed / kRunSpeed;
  if (crouch) strafe *= kCrouchSpeedScale;

  const Vector3 fwd = FlatForward(yaw);
  const Vector3 right = FlatRight(yaw);
  Vector3 delta = Vector3Add(Vector3Scale(fwd, speed), Vector3Scale(right, strafe));

  // ---------------------------------------------------------------- horizontal
  bool hitWall = false;
  const float h = height();
  pos = world.SlideMove(pos, delta, kPlayerRadius, h, &hitWall);
  if (hitWall) speed *= 0.5f;

  // GameMaker friction, applied after the move like GM's own step order.
  if (speed > 0.0f) speed = fmaxf(0.0f, speed - kFriction);
  else if (speed < 0.0f) speed = fminf(0.0f, speed + kFriction);

  // ------------------------------------------------------------------ jump
  if (in.jump && onGround && vy <= 0.0f) {
    vy = kJumpSpeed;
    onGround = false;
  }

  // -------------------------------------------------------------- vertical
  const float prevY = pos.y;
  prevVy_ = vy;
  pos.y += vy;
  vy -= kGravity;

  // Head bump.
  const float ceil = world.CeilingHeight(pos.x, pos.z, kPlayerRadius, prevY + h * 0.5f);
  if (pos.y + h > ceil) {
    pos.y = ceil - h;
    if (vy > 0.0f) vy = 0.0f;
  }

  // Land on the highest surface at or below where we were (plus a step).
  const float ground =
      world.GroundHeight(pos.x, pos.z, kPlayerRadius, prevY + kStepHeight);
  if (pos.y <= ground) {
    if (!onGround && prevVy_ < -1.2f) {
      landDip = fminf(4.0f, -prevVy_ * 1.2f);   // small camera dip on impact
      // Fall damage. GTJ3D had none -- you could step off the civic tower and
      // walk away -- which made every rooftop a free ride down. The threshold
      // is set from the jump: `kJumpSpeed` 2.6 against `kGravity` 0.15 tops
      // out about 22 units up and comes back down at 2.6 a tick, so anything
      // at or under that has to stay free or your own jump would hurt you.
      // Past it the damage goes with the square of the impact speed, which is
      // proportional to the height fallen, and by about 8 units a tick -- a
      // little over 200 units, five storeys -- it is fatal.
      landImpact = -prevVy_;
    }
    pos.y = ground;
    vy = 0.0f;
    onGround = true;
  } else {
    onGround = false;
  }

  // Keep inside the map.
  pos.x = Clampf(pos.x, kPlayerRadius, world.sizeX() - kPlayerRadius);
  pos.z = Clampf(pos.z, kPlayerRadius, world.sizeZ() - kPlayerRadius);
  if (pos.y < -200.0f) { pos.y = 0.0f; vy = 0.0f; }

  // ------------------------------------------------------------------- bob
  // Deliberately gentle: the phase advances at a fixed rate scaled by speed
  // (so the cadence does not stutter when you clip a wall), the amount eases
  // in and out slowly, and the resulting camera lift is low-passed. The old
  // version shook hard enough to be distracting at a run.
  const float horizSpeed = sqrtf(delta.x * delta.x + delta.z * delta.z);
  const float speedT = Clampf(horizSpeed / kRunSpeed, 0.0f, 1.0f);
  if (onGround && speedT > 0.04f) {
    bobPhase += (0.055f + 0.10f * speedT);
    bobAmount += (speedT - bobAmount) * 0.07f;
  } else {
    bobAmount *= 0.93f;
  }
  if (bobPhase > 2.0f * PI) bobPhase -= 2.0f * PI;

  // Two-beat footfall curve, then smoothed so there is no visible snap.
  const float targetLift = fabsf(sinf(bobPhase)) * 0.85f * bobAmount * (1.0f - crouchT * 0.5f);
  bobHeight += (targetLift - bobHeight) * 0.18f;
}

}  // namespace kaj
