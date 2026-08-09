// Weapon table and per-player ammo state.
//
// Fire rates are GTJ3D's `gun_reload(n)` cooldowns in ticks (pistol 20,
// assault rifle 4, shotgun 50, rocket 120, grenade 80). Damage is scaled up
// from GTJ's numbers so a 100 HP deathmatch plays like CS rather than like a
// GTA sandbox, and headshot/leg multipliers are layered on top.
#pragma once

#include "common.h"

namespace kaj {

struct WeaponDef {
  const char* name;
  const char* hudName;
  FireMode mode;

  float damage;
  int   cooldown;       // ticks between shots
  float range;
  int   pellets;
  float spread;         // degrees, cone half-angle, standing still
  float moveSpread;     // extra degrees at full run
  float recoil;         // pitch kick in degrees per shot

  int   magSize;        // 0 => uses `count` as a consumable
  int   reserveMax;
  int   reloadTicks;
  bool  shellReload;    // reload one at a time (shotgun)

  bool  canZoom;
  float zoomFov;
  float zoomSpread;

  // Projectile / explosive parameters (GTJ obj_bullet & obj_grenade).
  float projSpeed;      // units per tick
  float projGravity;    // units per tick^2
  float projUpBoost;    // extra vertical kick when thrown
  int   fuseTicks;      // 0 => detonate on impact
  float blastRadius;
  float blastDamage;

  const char* fireSound;
  float firePitch;      // pitch multiplier -- the sniper is a deepened rifle
  const char* viewmodel;
  int   fireAnimTicks;  // how long the shot animation runs, independent of the
                        // cooldown: the sniper flashes as fast as the rifle
                        // then simply sits idle for the rest of its 90 ticks.
                        // 0 means the viewmodel does not animate on firing.
  float vmScale;
  float vmOffsetX;
  float vmOffsetY;

  // Muzzle position in GTJ3D's 640x480 HUD space -- measured from where the
  // muzzle flash actually lights up in each sprite sheet. The world position
  // is unprojected from this, so tracers and barrel smoke leave the drawn
  // barrel exactly, centred on that gun's own muzzle.
  float muzzleHudX;
  float muzzleHudY;
  float smokeScale;     // barrel smoke puff size; 0 = none
  // Procedural kick applied to the viewmodel on firing, in HUD pixels. Sprite
  // sheets that already animate their own recoil use a small value or none;
  // the single-frame imported models rely on it entirely.
  float vmRecoil;
};

const WeaponDef& Weapon(int id);

// Per-player ammo / timing.
struct WeaponSlot {
  int mag = 0;
  int reserve = 0;
  int cooldown = 0;
  int reloadTimer = 0;
  bool reloading = false;
};

struct Arsenal {
  WeaponSlot slot[WEAPON_COUNT];
  uint8_t current = WEAPON_PISTOL;

  void ResetFull();
  void Tick();

  bool CanFire() const;
  bool NeedsReload() const;
  void BeginReload();
  void FinishReload();

  WeaponSlot& cur() { return slot[current]; }
  const WeaponSlot& cur() const { return slot[current]; }

  // Scroll wheel cycling, skipping anything with no ammo at all
  // (GTJ obj_gun's mouse_wheel_up/down + check_weapon loop).
  uint8_t NextUsable(int dir) const;
  bool HasAmmo(int id) const;
};

}  // namespace kaj
