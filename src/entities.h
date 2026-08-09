// Server-simulated entities: rockets, grenades, mines and smoke.
//
// Purely cosmetic effects (smoke, fire, debris, tracers, decals) live in fx.h.
#pragma once

#include <cstdint>
#include <vector>

#include "common.h"

namespace kaj {

enum EntityKind : uint8_t {
  ENT_ROCKET = 0,
  ENT_GRENADE,
  ENT_MINE,
  ENT_SMOKE,          // thrown; pops, then screens the area for 15 s
};

struct SimEntity {
  uint16_t id = 0;
  uint8_t kind = ENT_ROCKET;
  uint8_t owner = 0;
  Vector3 pos{};
  Vector3 vel{};
  Vector3 normal{0, 1, 0};   // mines: surface they cling to
  Vector3 beamEnd{};         // unused since the tripflare went
  int fuse = 0;              // ticks until self-detonation (0 = none)
  int arm = 0;               // placed: ticks until live. projectile: ticks alive
  bool alive = true;
};

// How long a popped smoke grenade screens the area, and how far it reaches.
// Fifteen seconds is long enough to cross a street under it and short enough
// that you cannot simply live in one.
constexpr int kSmokeTicks = 15 * 60;
constexpr float kSmokeRadius = 190.0f;

// Whether the segment a-b passes through a cloud thick enough to hide behind.
// Used by the bot vision test on the server and by nothing else -- the smoke
// you can see is particles, but what the AI sees has to be something both
// ends agree on, so it is this: a sphere around the canister that grows as
// the cloud blooms and fades as it thins.
bool SmokeBlocks(const std::vector<SimEntity>& ents, Vector3 a, Vector3 b);

// One-shot event the server broadcasts so every client can play the same bang.
struct BlastEvent {
  Vector3 pos{};
  float radius = 0.0f;
  uint8_t kind = ENT_ROCKET;
};

}  // namespace kaj
