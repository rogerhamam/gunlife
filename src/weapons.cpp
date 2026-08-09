#include "weapons.h"

namespace kaj {
namespace {

// Field order:
//   name, hudName, mode,
//   damage, cooldown, range, pellets, spread, moveSpread, recoil,
//   magSize, reserveMax, reloadTicks, shellReload,
//   canZoom, zoomFov, zoomSpread,
//   projSpeed, projGravity, projUpBoost, fuseTicks, blastRadius, blastDamage,
//   fireSound, firePitch, viewmodel, fireAnimTicks, vmScale, vmOffX, vmOffY,
//   muzzleHudX, muzzleHudY, smokeScale, vmRecoil
//
// Muzzle HUD coordinates come from measuring the flash centroid in each sprite
// sheet (tools/, see README) and converting through the sprite's authored
// origin at 2x scale anchored to (320, 224).
// clang-format off
const WeaponDef kWeapons[WEAPON_COUNT] = {
  // FISTS -- GTJ: gun_reload(30) + melee(), and the fists viewmodel it was
  // always drawing anyway.
  {"Fists", "Fists", FIRE_MELEE,
    40.0f, 30, 26.0f, 1, 0.0f, 0.0f, 0.0f,
    0, 0, 0, false,
    false, 70.0f, 0.0f,
    0,0,0,0, 0,0,
    "swing", 1.0f, "vm_knife", 30, 2.0f, 0.0f, 0.0f,
    320.0f, 360.0f, 0.0f, 0.0f},

  // PISTOL -- GTJ: gun_reload(20), snd_handgun.
  // fireAnimTicks 0: the pistol holds its idle frame when fired. Its sheet
  // kicks the whole gun up the screen, which read as a distracting bounce.
  {"Pistol", "Pistol", FIRE_SEMI,
    26.0f, 20, 900.0f, 1, 0.5f, 2.2f, 0.9f,
    12, 72, 90, false,
    false, 70.0f, 0.0f,
    0,0,0,0, 0,0,
    "pistol", 1.0f, "vm_pistol", 0, 2.0f, 0.0f, 0.0f,
    315.0f, 371.0f, 1.5f, 3.0f},

  // SMG -- GTJ's 4-tick assault rifle cadence (900 RPM).
  {"SMG", "SMG", FIRE_AUTO,
    17.0f, 4, 700.0f, 1, 1.1f, 3.4f, 0.45f,
    30, 150, 120, false,
    false, 70.0f, 0.0f,
    0,0,0,0, 0,0,
    "smg", 1.0f, "vm_rifle", 4, 1.7f, 0.0f, 14.0f,
    320.0f, 328.0f, 1.2f, 3.0f},

  // RIFLE -- 600 RPM full auto. (The semi-auto marksman rifle was removed.)
  {"Rifle", "Rifle", FIRE_AUTO,
    24.0f, 6, 1600.0f, 1, 0.7f, 3.6f, 0.85f,
    30, 120, 140, false,
    false, 70.0f, 0.0f,
    0,0,0,0, 0,0,
    "rifle", 1.0f, "vm_rifle", 6, 2.0f, 0.0f, 0.0f,
    320.0f, 330.0f, 1.6f, 2.0f},

  // SHOTGUN -- GTJ: gun_reload(50), snd_shotgun, shell-by-shell reload.
  {"Shotgun", "Shotgun", FIRE_SEMI,
    13.0f, 50, 450.0f, 8, 5.0f, 2.0f, 2.4f,
    8, 40, 26, true,
    false, 70.0f, 0.0f,
    0,0,0,0, 0,0,
    // The pump animation runs over most of the 50-tick cooldown so the cocking
    // action reads properly instead of snapping through.
    "shotgun", 1.0f, "vm_shotgun", 42, 2.0f, 0.0f, 0.0f,
    314.0f, 395.0f, 4.6f, 0.0f},

  // SUPER SHOTGUN -- both barrels at once, a wall of pellets, then the long
  // break-open reload the sprite sheet animates end to end.
  {"Super Shotgun", "Super Shotgun", FIRE_SEMI,
    12.0f, 62, 420.0f, 16, 7.5f, 2.4f, 4.2f,
    2, 24, 90, false,
    false, 70.0f, 0.0f,
    0,0,0,0, 0,0,
    "supershotgun", 1.0f, "vm_ssg", 62, 1.4f, 0.0f, 0.0f,
    320.0f, 372.0f, 5.6f, 14.0f},

  // SNIPER -- its own heavy report (nc_cannon_1), the rifle viewmodel firing
  // at the rifle's animation speed, and a long reload.
  {"Sniper", "Sniper", FIRE_SEMI,
    115.0f, 90, 3000.0f, 1, 5.5f, 4.0f, 3.2f,
    5, 25, 300, false,
    true, 18.0f, 0.02f,
    0,0,0,0, 0,0,
    "sniper", 1.0f, "vm_rifle", 6, 2.0f, 0.0f, 0.0f,
    320.0f, 330.0f, 4.2f, 9.0f},

  // ROCKET LAUNCHER -- GTJ: gun_reload(120), speed 8, blast_radius 80.
  {"Rocket Launcher", "Rockets", FIRE_PROJECTILE,
    0.0f, 120, 0.0f, 1, 0.0f, 0.0f, 4.0f,
    1, 8, 120, false,
    false, 70.0f, 0.0f,
    8.0f, 0.0f, 0.0f, 0, 190.0f, 260.0f,
    "rocket_fire", 1.0f, "vm_rocket", 40, 2.0f, 0.0f, 0.0f,
    313.0f, 318.0f, 5.0f, 0.0f},

  // GRENADE -- GTJ: gun_reload(80), speed 6, +2.5 up, gravity 0.2.
  // GTJ3D had no throw sound; the detonation uses snd_explosion, which is what
  // obj_explosion_effect set as control.play_sound.
  {"Grenade", "Grenades", FIRE_PROJECTILE,
    0.0f, 80, 0.0f, 1, 0.0f, 0.0f, 0.0f,
    0, 4, 0, false,
    false, 70.0f, 0.0f,
    6.0f, 0.2f, 2.5f, 150, 175.0f, 170.0f,
    "swing", 1.15f, "vm_grenade", 30, 2.0f, 0.0f, 0.0f,
    320.0f, 360.0f, 0.0f, 0.0f},

  // PROXIMITY MINE -- placed at your feet, arms after a second.
  {"Proximity Mine", "Mines", FIRE_PLACE,
    0.0f, 60, 0.0f, 1, 0.0f, 0.0f, 0.0f,
    0, 3, 0, false,
    false, 70.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0, 120.0f, 115.0f,
    "place", 1.0f, "vm_knife", 30, 2.0f, 0.0f, 0.0f,
    320.0f, 360.0f, 0.0f, 0.0f},

  // SMOKE GRENADE -- thrown like a frag, but it pops rather than detonates:
  // a small burst, then fifteen seconds of screening smoke. It does no damage
  // at all, so the blast radius is zero and the "explosion" is cosmetic; what
  // it is for is breaking line of sight, and the AI genuinely cannot see
  // through it. Thrown a little harder and flatter than a frag, because you
  // want it landing where you are going rather than at your feet.
  {"Smoke Grenade", "Smoke", FIRE_PROJECTILE,
    0.0f, 70, 0.0f, 1, 0.0f, 0.0f, 0.0f,
    0, 3, 0, false,
    false, 70.0f, 0.0f,
    7.0f, 0.2f, 1.8f, 260, 0.0f, 0.0f,
    "swing", 1.15f, "vm_grenade", 30, 2.0f, 0.0f, 0.0f,
    320.0f, 360.0f, 0.0f, 0.0f},
};
// clang-format on

}  // namespace

const WeaponDef& Weapon(int id) {
  if (id < 0 || id >= WEAPON_COUNT) id = WEAPON_FISTS;
  return kWeapons[id];
}

void Arsenal::ResetFull() {
  for (int i = 0; i < WEAPON_COUNT; ++i) {
    const WeaponDef& d = Weapon(i);
    slot[i] = WeaponSlot{};
    slot[i].mag = d.magSize;
    slot[i].reserve = d.reserveMax;
  }
  current = WEAPON_PISTOL;
}

void Arsenal::Tick() {
  for (int i = 0; i < WEAPON_COUNT; ++i) {
    WeaponSlot& s = slot[i];
    if (s.cooldown > 0) --s.cooldown;
    if (s.reloading) {
      if (--s.reloadTimer <= 0) {
        // Shell reloads top up one round at a time and keep going.
        const WeaponDef& d = Weapon(i);
        const int want = d.shellReload ? 1 : (d.magSize - s.mag);
        const int take = want < s.reserve ? want : s.reserve;
        s.mag += take;
        s.reserve -= take;
        if (d.shellReload && s.mag < d.magSize && s.reserve > 0) {
          s.reloadTimer = d.reloadTicks;
        } else {
          s.reloading = false;
        }
      }
    }
  }
}

bool Arsenal::HasAmmo(int id) const {
  const WeaponDef& d = Weapon(id);
  if (d.mode == FIRE_MELEE) return true;
  if (d.magSize == 0) return slot[id].reserve > 0;
  return slot[id].mag > 0 || slot[id].reserve > 0;
}

bool Arsenal::CanFire() const {
  const WeaponSlot& s = cur();
  if (s.cooldown > 0) return false;
  const WeaponDef& d = Weapon(current);
  if (d.mode == FIRE_MELEE) return true;
  if (s.reloading) return false;
  if (d.magSize == 0) return s.reserve > 0;
  return s.mag > 0;
}

bool Arsenal::NeedsReload() const {
  const WeaponDef& d = Weapon(current);
  if (d.magSize == 0 || d.mode == FIRE_MELEE) return false;
  return cur().mag <= 0 && cur().reserve > 0;
}

void Arsenal::BeginReload() {
  const WeaponDef& d = Weapon(current);
  WeaponSlot& s = cur();
  if (d.magSize == 0 || d.mode == FIRE_MELEE) return;
  if (s.reloading || s.mag >= d.magSize || s.reserve <= 0) return;
  s.reloading = true;
  s.reloadTimer = d.reloadTicks;
}

void Arsenal::FinishReload() {
  WeaponSlot& s = cur();
  s.reloading = false;
  s.reloadTimer = 0;
}

uint8_t Arsenal::NextUsable(int dir) const {
  int w = current;
  for (int i = 0; i < WEAPON_COUNT; ++i) {
    w += dir;
    if (w >= WEAPON_COUNT) w = 0;
    if (w < 0) w = WEAPON_COUNT - 1;
    if (HasAmmo(w)) return static_cast<uint8_t>(w);
  }
  return static_cast<uint8_t>(WEAPON_FISTS);
}

}  // namespace kaj
