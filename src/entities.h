// Server-simulated entities: rockets, grenades, mines and tripflares.
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
  ENT_TRIPFLARE,
};

struct SimEntity {
  uint16_t id = 0;
  uint8_t kind = ENT_ROCKET;
  uint8_t owner = 0;
  Vector3 pos{};
  Vector3 vel{};
  Vector3 normal{0, 1, 0};   // mines/tripflares: surface they cling to
  Vector3 beamEnd{};         // tripflare beam terminus
  int fuse = 0;              // ticks until self-detonation (0 = none)
  int arm = 0;               // placed: ticks until live. projectile: ticks alive
  bool alive = true;
};

// One-shot event the server broadcasts so every client can play the same bang.
struct BlastEvent {
  Vector3 pos{};
  float radius = 0.0f;
  uint8_t kind = ENT_ROCKET;
};

}  // namespace kaj
