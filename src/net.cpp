#include "net.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "world.h"

// raylib and windows.h both define Rectangle/CloseWindow/ShowCursor. raylib is
// already included above via net.h, so shut GDI and USER out of windows.h --
// the sockets API lives in neither.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#undef near
#undef far

namespace kaj {
namespace {

constexpr int kMaxPacket = 1400;
constexpr double kTimeout = 8.0;

// ------------------------------------------------------------ serialisation
struct Writer {
  uint8_t buf[kMaxPacket];
  int len = 0;
  void u8(uint8_t v)  { if (len + 1 <= kMaxPacket) buf[len++] = v; }
  void u16(uint16_t v){ raw(&v, 2); }
  void u32(uint32_t v){ raw(&v, 4); }
  void i16(int16_t v) { raw(&v, 2); }
  void f32(float v)   { raw(&v, 4); }
  void raw(const void* p, int n) {
    if (len + n > kMaxPacket) return;
    memcpy(buf + len, p, n);
    len += n;
  }
  void str(const std::string& s, int fixed) {
    char tmp[64] = {0};
    const int n = fixed < 64 ? fixed : 63;
    strncpy(tmp, s.c_str(), n - 1);
    raw(tmp, n);
  }
};

struct Reader {
  const uint8_t* p;
  int len, off = 0;
  Reader(const uint8_t* d, int n) : p(d), len(n) {}
  bool ok(int n) const { return off + n <= len; }
  uint8_t u8()  { if (!ok(1)) return 0; return p[off++]; }
  uint16_t u16(){ uint16_t v = 0; raw(&v, 2); return v; }
  uint32_t u32(){ uint32_t v = 0; raw(&v, 4); return v; }
  int16_t i16() { int16_t v = 0; raw(&v, 2); return v; }
  float f32()   { float v = 0; raw(&v, 4); return v; }
  void raw(void* d, int n) {
    if (!ok(n)) { memset(d, 0, n); off = len; return; }
    memcpy(d, p + off, n);
    off += n;
  }
  std::string str(int fixed) {
    char tmp[64] = {0};
    const int n = fixed < 64 ? fixed : 63;
    raw(tmp, n);
    tmp[n - 1] = 0;
    return std::string(tmp);
  }
};

void WriteEvent(Writer& w, const NetEvent& e) {
  w.u32(e.seq); w.u8(e.type); w.u8(e.a); w.u8(e.b); w.u8(e.weapon); w.u8(e.flags);
  w.f32(e.x); w.f32(e.y); w.f32(e.z);
  w.f32(e.dx); w.f32(e.dy); w.f32(e.dz);
  w.f32(e.value);
}

NetEvent ReadEvent(Reader& r) {
  NetEvent e;
  e.seq = r.u32(); e.type = r.u8(); e.a = r.u8(); e.b = r.u8();
  e.weapon = r.u8(); e.flags = r.u8();
  e.x = r.f32(); e.y = r.f32(); e.z = r.f32();
  e.dx = r.f32(); e.dy = r.f32(); e.dz = r.f32();
  e.value = r.f32();
  return e;
}

bool g_netUp = false;

// Distance from point `p` to segment `a`-`b`.
float SegmentDist(Vector3 p, Vector3 a, Vector3 b) {
  const Vector3 ab = Vector3Subtract(b, a);
  const float denom = Vector3DotProduct(ab, ab);
  if (denom < 0.0001f) return Vector3Distance(p, a);
  float t = Vector3DotProduct(Vector3Subtract(p, a), ab) / denom;
  t = Clampf(t, 0.0f, 1.0f);
  return Vector3Distance(p, Vector3Add(a, Vector3Scale(ab, t)));
}

}  // namespace

// A sphere around each popped canister that blooms over the first second and
// thins away over the last three. A sight line that passes through one is
// blocked -- which is what "the AI cannot see you through smoke" has to mean
// mechanically, because a particle cloud is a client-side thing and both ends
// have to agree.
bool SmokeBlocks(const std::vector<SimEntity>& ents, Vector3 a, Vector3 b) {
  for (const SimEntity& e : ents) {
    if (!e.alive || e.kind != ENT_SMOKE || e.arm >= 0) continue;
    // `arm` runs from -kSmokeTicks up to 0 while it vents.
    const int left = -e.arm;                       // ticks of life remaining
    const int age = kSmokeTicks - left;
    // Bloom over the first second, hold, then thin over the last three.
    float t = 1.0f;
    if (age < 60) t = age / 60.0f;
    else if (left < 180) t = left / 180.0f;
    const float r = kSmokeRadius * t;
    if (r < 24.0f) continue;
    if (SegmentDist(e.pos, a, b) < r) return true;
  }
  return false;
}

// ----------------------------------------------------------------- sockets

std::string Endpoint::ToString() const {
  in_addr a;
  a.s_addr = ip;
  char b[64];
  snprintf(b, sizeof(b), "%s:%u", inet_ntoa(a), (unsigned)port);
  return std::string(b);
}

bool NetInit() {
  if (g_netUp) return true;
  WSADATA d;
  if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
  g_netUp = true;
  return true;
}

void NetShutdown() {
  if (!g_netUp) return;
  WSACleanup();
  g_netUp = false;
}

bool UdpSocket::Open(uint16_t port) {
  Close();
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return false;

  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons(port);
  if (bind(s, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) {
    closesocket(s);
    return false;
  }
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);

  int addrLen = sizeof(a);
  if (getsockname(s, (sockaddr*)&a, &addrLen) == 0) localPort_ = ntohs(a.sin_port);
  else localPort_ = port;

  handle_ = static_cast<unsigned long long>(s);
  return true;
}

void UdpSocket::Close() {
  if (handle_ == ~0ull) return;
  closesocket(static_cast<SOCKET>(handle_));
  handle_ = ~0ull;
  localPort_ = 0;
}

bool UdpSocket::Send(const Endpoint& to, const void* data, int len) {
  if (!valid()) return false;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = to.ip;
  a.sin_port = htons(to.port);
  return sendto(static_cast<SOCKET>(handle_), (const char*)data, len, 0,
                (sockaddr*)&a, sizeof(a)) != SOCKET_ERROR;
}

int UdpSocket::Recv(void* buf, int maxLen, Endpoint* from) {
  if (!valid()) return -1;
  sockaddr_in a{};
  int alen = sizeof(a);
  const int n = recvfrom(static_cast<SOCKET>(handle_), (char*)buf, maxLen, 0,
                         (sockaddr*)&a, &alen);
  if (n <= 0) return -1;
  if (from) { from->ip = a.sin_addr.s_addr; from->port = ntohs(a.sin_port); }
  return n;
}

bool ResolveHost(const std::string& host, uint16_t port, Endpoint* out) {
  if (!NetInit()) return false;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  out->ip = ((sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
  out->port = port;
  freeaddrinfo(res);
  return true;
}

// ------------------------------------------------------------------ server

bool Server::Start(uint16_t port, const World* world, const std::string& mapName,
                   int bots) {
  if (!NetInit()) return false;
  if (!socket_.Open(port)) return false;
  world_ = world;
  mapName_ = mapName;
  running_ = true;
  tick_ = 0;
  ents_.clear();
  events_.clear();

  static const char* kBotNames[] = {"Rook", "Vex", "Dice", "Marlow", "Sable",
                                    "Quill", "Hark", "Nyx"};
  for (int i = 0; i < bots && i < kMaxPlayers - 1; ++i) {
    // Bots take the high slots so human players get low, stable ids.
    const int s = kMaxPlayers - 1 - i;
    slots_[s].used = true;
    slots_[s].bot = true;
    slots_[s].botArsenal.ResetFull();
    slots_[s].botHasLauncher = GetRandomValue(0, 4) == 0;
    slots_[s].state = PlayerState{};
    slots_[s].state.id = static_cast<uint8_t>(s);
    slots_[s].state.active = 1;
    slots_[s].state.team = 1;
    snprintf(slots_[s].state.name, sizeof(slots_[s].state.name), "[BOT] %s",
             kBotNames[i % 8]);
    RespawnPlayer(s);
  }
  TraceLog(LOG_INFO, "SERVER: listening on %u (%d bots)", (unsigned)socket_.localPort(),
           bots);
  return true;
}

int Server::aliveBots() const {
  int n = 0;
  for (const ServerSlot& s : slots_)
    if (s.used && s.bot && !s.state.dead()) ++n;
  return n;
}

bool Server::SpawnBotAt(Vector3 pos, float yawDeg, float skill, float health) {
  static const char* kSquad[] = {"Alpha", "Bravo", "Charlie", "Delta",
                                 "Echo", "Foxtrot", "Golf", "Hotel"};
  for (int i = kMaxPlayers - 1; i >= 1; --i) {
    ServerSlot& s = slots_[i];
    if (s.used) continue;
    s = ServerSlot{};
    s.used = true;
    s.bot = true;
    s.botArsenal.ResetFull();
    // One in five carries a launcher; the rest have grenades and that is all.
    s.botHasLauncher = GetRandomValue(0, 4) == 0;
    s.botSkill = Clampf(skill, 0.0f, 1.0f);
    s.botHealth = health;
    s.state = PlayerState{};
    s.state.id = static_cast<uint8_t>(i);
    s.state.active = 1;
    s.state.team = 1;
    snprintf(s.state.name, sizeof(s.state.name), "[SWAT] %s", kSquad[i % 8]);
    // Placed, not spawned: RespawnPlayer would send it off to a spawn point
    // on the far side of the map, and the whole point of a van is that the
    // squad arrives where the van stopped.
    RespawnPlayer(i);
    s.state.setPos(pos);
    s.state.yaw = yawDeg;
    s.state.pitch = 0.0f;
    BroadcastRoster();
    return true;
  }
  return false;
}

int Server::SetBotPopulation(int want, float skill, float health) {
  static const char* kNames[] = {"Rook", "Vex", "Dice", "Marlow", "Sable",
                                 "Quill", "Hark", "Nyx", "Corvo", "Wren",
                                 "Bram"};
  if (want < 0) want = 0;

  // Clear out anything already down. With respawn off this is what frees a
  // slot for the next reinforcement.
  if (!botRespawn_) {
    for (int i = 0; i < kMaxPlayers; ++i) {
      ServerSlot& s = slots_[i];
      if (s.used && s.bot && s.state.dead() && --s.respawnTimer <= 0) {
        s = ServerSlot{};
      }
    }
  }

  int live = aliveBots();
  int added = 0;
  // Bots take the high slots so human players keep low, stable ids.
  for (int i = kMaxPlayers - 1; i >= 1 && live < want; --i) {
    ServerSlot& s = slots_[i];
    if (s.used) continue;
    s = ServerSlot{};
    s.used = true;
    s.bot = true;
    s.botArsenal.ResetFull();
    // One in five carries a launcher; the rest have grenades and that is all.
    s.botHasLauncher = GetRandomValue(0, 4) == 0;
    s.botSkill = Clampf(skill, 0.0f, 1.0f);
    s.botHealth = health;
    s.state = PlayerState{};
    s.state.id = static_cast<uint8_t>(i);
    s.state.active = 1;
    s.state.team = 1;
    snprintf(s.state.name, sizeof(s.state.name), "[SWAT] %s",
             kNames[(i + added) % 11]);
    RespawnPlayer(i);
    ++live;
    ++added;
  }
  // Trimming down: take the ones nobody is looking at, from the top.
  for (int i = 1; i < kMaxPlayers && live > want; ++i) {
    ServerSlot& s = slots_[i];
    if (!s.used || !s.bot || s.state.dead()) continue;
    s = ServerSlot{};
    --live;
  }
  // Retune whoever is left -- a wave that steps up mid-fight should not leave
  // last wave's conscripts standing.
  for (ServerSlot& s : slots_) {
    if (!s.used || !s.bot) continue;
    s.botSkill = Clampf(skill, 0.0f, 1.0f);
    s.botHealth = health;
  }
  if (added > 0) BroadcastRoster();
  return added;
}

void Server::Stop() {
  if (!running_) return;
  socket_.Close();
  running_ = false;
  for (ServerSlot& s : slots_) s = ServerSlot{};
}

int Server::playerCount() const {
  int n = 0;
  for (const ServerSlot& s : slots_) if (s.used) ++n;
  return n;
}

int Server::FindSlot(const Endpoint& e) const {
  for (int i = 0; i < kMaxPlayers; ++i)
    if (slots_[i].used && !slots_[i].bot && slots_[i].addr == e) return i;
  return -1;
}

int Server::AllocSlot() {
  for (int i = 0; i < kMaxPlayers; ++i) if (!slots_[i].used) return i;
  return -1;
}

void Server::PushEvent(const NetEvent& e) {
  NetEvent ev = e;
  ev.seq = eventSeq_++;
  events_.push_back(ev);
  while (events_.size() > 64) events_.pop_front();
}

void Server::RespawnPlayer(int slot) {
  ServerSlot& s = slots_[slot];
  const bool waveEnemy = s.bot && !botRespawn_;

  // Pick the spawn furthest from any living player -- except for a wave
  // enemy, which is supposed to be coming *at* you. Those pick the spawn
  // closest to a comfortable assault distance from the nearest human, so a
  // wave arrives on the same block rather than at the far end of the city and
  // then spends two minutes walking.
  constexpr float kAssaultRange = 900.0f;
  int best = 0;
  float bestScore = -1.0f;
  const int n = world_ ? (int)world_->spawns().size() : 0;
  for (int i = 0; i < n; ++i) {
    const Vector3 p = world_->spawns()[i].pos;
    float nearest = 1e9f;
    for (int j = 0; j < kMaxPlayers; ++j) {
      if (j == slot || !slots_[j].used || slots_[j].state.dead()) continue;
      if (waveEnemy && slots_[j].bot) continue;   // measure against humans
      nearest = fminf(nearest, Vector3Distance(p, slots_[j].state.pos()));
    }
    const float jitter = static_cast<float>(GetRandomValue(0, 120));
    // Furthest wins for a normal respawn; nearest to assault range wins for a
    // wave enemy, so `score` is negated distance-from-ideal in that case.
    const float score = waveEnemy && nearest < 1e8f
                            ? -fabsf(nearest - kAssaultRange) + jitter
                            : nearest + jitter;
    if (score > bestScore) { bestScore = score; best = i; }
  }

  const SpawnPoint sp = world_ ? world_->PickSpawn(best) : SpawnPoint{};
  s.state.setPos(sp.pos);
  s.state.yaw = sp.yaw;
  s.state.pitch = 0.0f;
  // Wave enemies carry whatever the wave gave them; everyone else the usual
  // hundred. Anything over a hundred rides in the armour bar so the HUD, the
  // damage model and the hit feedback all keep working unchanged.
  s.state.health = kMaxHealth;
  s.state.armor = 0.0f;
  if (s.bot && s.botHealth > kMaxHealth)
    s.state.armor = fminf(kMaxArmor, s.botHealth - kMaxHealth);
  s.state.flags = 0;
  s.state.weapon = WEAPON_PISTOL;
  s.respawnTimer = 0;
  s.botArsenal.ResetFull();
  s.botVy = 0.0f;

  NetEvent ev;
  ev.type = EV_SPAWN;
  ev.a = static_cast<uint8_t>(slot);
  ev.x = sp.pos.x; ev.y = sp.pos.y; ev.z = sp.pos.z;
  ev.value = sp.yaw;
  PushEvent(ev);
}

void Server::KillPlayer(int victim, int killer, int weapon, bool headshot) {
  ServerSlot& v = slots_[victim];
  if (v.state.dead()) return;
  v.state.health = 0.0f;
  v.state.flags |= PF_DEAD;
  v.state.deaths++;
  v.respawnTimer = static_cast<int>(kRespawnTime * kTickRate);
  if (killer >= 0 && killer < kMaxPlayers && killer != victim && slots_[killer].used)
    slots_[killer].state.kills++;
  else if (killer == victim)
    v.state.kills--;   // suicide by your own explosive

  NetEvent ev;
  ev.type = EV_DEATH;
  ev.a = static_cast<uint8_t>(victim);
  ev.b = static_cast<uint8_t>(killer < 0 ? victim : killer);
  ev.weapon = static_cast<uint8_t>(weapon);
  ev.flags = headshot ? 1 : 0;
  ev.x = v.state.x; ev.y = v.state.y; ev.z = v.state.z;
  // Direction of the killing blow and how far up the body it landed, so the
  // corpse recoils and folds the right way.
  ev.dx = v.lastHitDir.x; ev.dy = v.lastHitDir.y; ev.dz = v.lastHitDir.z;
  ev.value = v.lastHitHeight;
  PushEvent(ev);
}

void Server::SpawnProjectile(int owner, int weapon, Vector3 origin, Vector3 dir) {
  if (ents_.size() >= kMaxNetEnts) return;
  const WeaponDef& d = Weapon(weapon);
  SimEntity e;
  e.id = nextEntId_++;
  e.owner = static_cast<uint8_t>(owner);
  e.kind = (weapon == WEAPON_ROCKET)  ? ENT_ROCKET
           : (weapon == WEAPON_SMOKE) ? ENT_SMOKE
                                      : ENT_GRENADE;
  e.pos = origin;
  e.vel = Vector3Scale(Vector3Normalize(dir), d.projSpeed);
  e.vel.y += d.projUpBoost;        // GTJ added 2.5 of lift to thrown grenades
  e.fuse = d.fuseTicks;
  ents_.push_back(e);
}

void Server::PlaceCharge(int owner, int weapon, Vector3 pos, Vector3 normal,
                         Vector3 beamEnd) {
  if (ents_.size() >= kMaxNetEnts) return;
  SimEntity e;
  e.id = nextEntId_++;
  e.owner = static_cast<uint8_t>(owner);
  e.kind = ENT_MINE;
  e.pos = pos;
  e.normal = normal;
  e.beamEnd = beamEnd;
  e.arm = 60;                      // one second before it goes live
  e.fuse = 0;
  ents_.push_back(e);
}

void Server::ApplyBlast(Vector3 pos, float radius, float damage, int owner,
                        int weapon, uint8_t kind) {
  NetEvent ev;
  ev.type = EV_BLAST;
  ev.b = kind;
  ev.x = pos.x; ev.y = pos.y; ev.z = pos.z;
  ev.value = radius;
  ev.weapon = static_cast<uint8_t>(weapon);
  PushEvent(ev);

  const bool ownerIsBot =
      owner >= 0 && owner < kMaxPlayers && slots_[owner].used && slots_[owner].bot;

  for (int i = 0; i < kMaxPlayers; ++i) {
    ServerSlot& s = slots_[i];
    if (!s.used || s.state.dead()) continue;
    // A bot's own explosives never hurt the rest of the squad -- now that
    // they throw grenades and carry launchers, one careless rocket would take
    // out half a wave. It still hurts the thrower, which is what stops them
    // firing a launcher into a wall a foot in front of their face.
    if (ownerIsBot && s.bot && i != owner) continue;
    const Vector3 center =
        Vector3Add(s.state.pos(), Vector3{0, s.state.height() * 0.5f, 0});
    const float dist = Vector3Distance(center, pos);
    if (dist > radius) continue;
    if (world_ && !world_->LineOfSight(pos, center)) continue;

    float dmg = damage * (1.0f - dist / radius);
    if (i == owner) dmg *= 0.65f;          // you feel your own rockets, a bit less
    if (dmg <= 0.5f) continue;

    // A blast throws you away from it, from about waist height.
    {
      Vector3 d = Vector3Subtract(center, pos);
      s.lastHitDir = (Vector3LengthSqr(d) > 0.01f) ? Vector3Normalize(d)
                                                   : Vector3{0, 1, 0};
      s.lastHitHeight = 0.5f;
    }

    if (s.state.armor > 0.0f) {
      const float absorbed = fminf(dmg * 0.5f, s.state.armor);
      s.state.armor -= absorbed;
      dmg -= absorbed;
    }
    s.state.health -= dmg;

    NetEvent hv;
    hv.type = EV_HIT;
    hv.a = static_cast<uint8_t>(owner);
    hv.b = static_cast<uint8_t>(i);
    hv.weapon = static_cast<uint8_t>(weapon);
    hv.value = dmg;
    hv.x = center.x; hv.y = center.y; hv.z = center.z;
    PushEvent(hv);

    if (s.state.health <= 0.0f) KillPlayer(i, owner, weapon, false);
  }
}

void Server::Detonate(const SimEntity& e) {
  int weapon = WEAPON_ROCKET;
  if (e.kind == ENT_GRENADE) weapon = WEAPON_GRENADE;
  else if (e.kind == ENT_MINE) weapon = WEAPON_MINE;
  else if (e.kind == ENT_SMOKE) weapon = WEAPON_SMOKE;
  const WeaponDef& d = Weapon(weapon);
  ApplyBlast(e.pos, d.blastRadius, d.blastDamage, e.owner, weapon, e.kind);
}

void Server::SimulateEntities() {
  for (SimEntity& e : ents_) {
    if (!e.alive) continue;

    // A smoke canister that has already popped just sits there venting; it
    // is handled with the other placed entities below.
    const bool flying = e.kind == ENT_ROCKET || e.kind == ENT_GRENADE ||
                        (e.kind == ENT_SMOKE && e.arm >= 0);
    if (flying) {
      const WeaponDef& d = Weapon(e.kind == ENT_ROCKET  ? WEAPON_ROCKET
                                  : e.kind == ENT_SMOKE ? WEAPON_SMOKE
                                                        : WEAPON_GRENADE);
      ++e.arm;   // for projectiles `arm` counts ticks alive
      e.vel.y -= d.projGravity;
      const Vector3 next = Vector3Add(e.pos, e.vel);

      // World collision along the step.
      bool impact = false;
      Vector3 impactPoint = next;
      if (world_) {
        Vector3 dir = Vector3Subtract(next, e.pos);
        const float len = Vector3Length(dir);
        if (len > 0.0001f) {
          const RayHit h = world_->Raycast(e.pos, Vector3Scale(dir, 1.0f / len), len);
          if (h.hit) { impact = true; impactPoint = Vector3Add(h.point, Vector3Scale(h.normal, 2.0f)); }
        }
      }
      // Direct hit on a player.
      if (!impact) {
        for (int i = 0; i < kMaxPlayers; ++i) {
          const ServerSlot& s = slots_[i];
          if (!s.used || s.state.dead()) continue;
          // Do not let the shooter eat their own rocket as it leaves the tube.
          if (i == e.owner && e.arm < 8) continue;
          const Vector3 c = Vector3Add(s.state.pos(),
                                       Vector3{0, s.state.height() * 0.5f, 0});
          if (SegmentDist(c, e.pos, next) < kPlayerRadius + 4.0f) {
            impact = true;
            impactPoint = c;
            break;
          }
        }
      }

      e.pos = impact ? impactPoint : next;
      if (e.fuse > 0) --e.fuse;

      // GTJ's grenades and rockets both detonate on contact. The age cap stops
      // a rocket fired at the sky from living forever.
      if (impact || (d.fuseTicks > 0 && e.fuse <= 0) || e.pos.y < -50.0f ||
          e.arm > 8 * kTickRate) {
        if (e.kind == ENT_SMOKE) {
          // It pops rather than detonating: no damage, and the canister
          // stays exactly where it stopped, venting, until the cloud is
          // gone. `arm` goes negative to mark it as popped and counts the
          // ticks it has left -- fifteen seconds of screen.
          if (impact) {
            e.pos = impactPoint;
            // Settle it onto the ground rather than leaving it floating two
            // units off whatever it clipped.
            if (world_)
              e.pos.y = world_->GroundHeight(e.pos.x, e.pos.z, 3.0f,
                                             e.pos.y + 8.0f) + 2.0f;
          }
          e.vel = Vector3{0, 0, 0};
          e.arm = -kSmokeTicks;
          e.fuse = 0;
          // One event so every client makes the same pop in the same place.
          NetEvent pe;
          pe.type = EV_BLAST;
          pe.b = ENT_SMOKE;
          pe.weapon = WEAPON_SMOKE;
          pe.x = e.pos.x; pe.y = e.pos.y; pe.z = e.pos.z;
          pe.value = kSmokeRadius;
          PushEvent(pe);
        } else {
          Detonate(e);
          e.alive = false;
        }
      }
    } else if (e.kind == ENT_SMOKE) {
      // Popped and venting. It dies when the cloud has thinned to nothing.
      if (++e.arm >= 0) e.alive = false;
    } else {
      if (e.arm > 0) { --e.arm; continue; }

      if (e.kind == ENT_MINE) {
        for (int i = 0; i < kMaxPlayers; ++i) {
          const ServerSlot& s = slots_[i];
          if (!s.used || s.state.dead()) continue;
          if (i == e.owner) continue;   // your own mine ignores you
          const Vector3 c = Vector3Add(s.state.pos(),
                                       Vector3{0, s.state.height() * 0.5f, 0});
          if (Vector3Distance(c, e.pos) < 46.0f) {
            Detonate(e);
            e.alive = false;
            break;
          }
        }
      }
    }
  }

  ents_.erase(std::remove_if(ents_.begin(), ents_.end(),
                             [](const SimEntity& e) { return !e.alive; }),
              ents_.end());
}

void Server::UpdateBots() {
  for (int i = 0; i < kMaxPlayers; ++i) {
    ServerSlot& s = slots_[i];
    if (!s.used || !s.bot) continue;
    if (s.state.dead()) continue;
    s.botArsenal.Tick();

    // ------------------------------------------------ pick a target
    if (--s.botThink <= 0) {
      s.botThink = 20;
      s.botTarget = -1;
      float best = 1e9f;
      for (int j = 0; j < kMaxPlayers; ++j) {
        if (j == i || !slots_[j].used || slots_[j].state.dead()) continue;
        // Bots are one side. Left on the deathmatch rule of "shoot whoever
        // is nearest", a squad arriving together guns itself down in the
        // street before it reaches anybody -- which is what the kill feed
        // was full of.
        if (slots_[j].bot) continue;
        const float d = Vector3Distance(s.state.pos(), slots_[j].state.pos());
        const Vector3 from = Vector3Add(s.state.pos(), Vector3{0, kPlayerEye, 0});
        const Vector3 to = slots_[j].state.eyePos();
        // Smoke breaks the sight line as surely as a wall does. Without this
        // a screening grenade would be decoration -- the cloud would hide
        // them from you while they carried on shooting straight through it.
        if (d < best && world_ && world_->LineOfSight(from, to) &&
            !SmokeBlocks(ents_, from, to)) {
          best = d;
          s.botTarget = j;
        }
      }
      s.botStrafe = static_cast<float>(GetRandomValue(-1, 1));
      // Bots favour a rifle up close, sniper at range. A good one reaches for
      // a shotgun in a knife fight and does not waste a sniper indoors.
      if (best > 900.0f) s.botArsenal.current = WEAPON_SNIPER;
      else if (best > 260.0f) s.botArsenal.current = WEAPON_RIFLE;
      else if (best < 110.0f && s.botSkill > 0.55f)
        s.botArsenal.current = WEAPON_SHOTGUN;
      else s.botArsenal.current = WEAPON_SMG;
      s.state.weapon = s.botArsenal.current;
      // Better ones reassess more often, which is most of what makes them
      // hard to break contact with.
      s.botThink = 20 - static_cast<int>(s.botSkill * 10.0f);
    }

    Vector3 wish{0, 0, 0};
    if (s.botTarget >= 0 && slots_[s.botTarget].used &&
        !slots_[s.botTarget].state.dead()) {
      const PlayerState& t = slots_[s.botTarget].state;
      const Vector3 me = Vector3Add(s.state.pos(), Vector3{0, kPlayerEye, 0});
      const Vector3 to = Vector3Subtract(t.eyePos(), me);
      const float dist = Vector3Length(to);

      const float wantYaw = atan2f(-to.z, to.x) * RAD2DEG;
      float diff = wantYaw - s.state.yaw;
      while (diff > 180.0f) diff -= 360.0f;
      while (diff < -180.0f) diff += 360.0f;
      // Finite turn rate: a wave-one conscript swings round slowly, a late
      // one snaps onto you.
      const float turnRate = 4.5f + s.botSkill * 8.0f;
      s.state.yaw += Clampf(diff, -turnRate, turnRate);
      s.state.pitch = Clampf(atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z)) * RAD2DEG,
                             -kPitchLimit, kPitchLimit);

      // Close the gap, but keep some distance and strafe.
      const Vector3 fwd = FlatForward(s.state.yaw);
      const Vector3 right = FlatRight(s.state.yaw);
      const float approach = dist > 320.0f ? 1.0f : (dist < 140.0f ? -0.6f : 0.0f);
      wish = Vector3Add(Vector3Scale(fwd, approach * 2.1f),
                        Vector3Scale(right, s.botStrafe * 1.6f));

      // ---- grenades and launchers ---------------------------------------
      // Rate-limited per bot rather than by ammunition, so a squad lays down
      // the occasional rocket instead of a constant barrage. Both have a
      // minimum range: a bot that fires a launcher into somebody's chest
      // takes the blast itself, and its own explosives are the only ones
      // that can still hurt it.
      if (s.botHeavyDelay > 0) --s.botHeavyDelay;
      if (s.botHeavyDelay == 0 && fabsf(diff) < 10.0f) {
        int heavy = -1;
        if (s.botHasLauncher && dist > 300.0f && dist < 1500.0f &&
            GetRandomValue(0, 1) == 0)
          heavy = WEAPON_ROCKET;
        else if (dist > 170.0f && dist < 620.0f)
          heavy = WEAPON_GRENADE;

        if (heavy >= 0 && world_ && world_->LineOfSight(me, t.eyePos())) {
          // Better shots lead less badly and reload quicker.
          s.botHeavyDelay = 210 + GetRandomValue(0, 260) -
                            static_cast<int>(s.botSkill * 120.0f);
          // A grenade is lobbed: aim above the target and let gravity bring
          // it down. A rocket flies flat.
          Vector3 aim = Vector3Normalize(to);
          if (heavy == WEAPON_GRENADE)
            aim = Vector3Normalize(
                Vector3Add(aim, Vector3{0.0f, 0.18f + dist / 4000.0f, 0.0f}));
          const float sp = (3.0f - s.botSkill * 1.8f) * kSpreadScale;
          aim = Vector3Normalize(Vector3Add(
              aim, Vector3{RandRange(-sp, sp) * 0.017f,
                           RandRange(-sp, sp) * 0.017f,
                           RandRange(-sp, sp) * 0.017f}));

          NetEvent fe;
          fe.type = EV_FIRE;
          fe.a = static_cast<uint8_t>(i);
          fe.weapon = static_cast<uint8_t>(heavy);
          fe.x = me.x; fe.y = me.y; fe.z = me.z;
          fe.dx = aim.x; fe.dy = aim.y; fe.dz = aim.z;
          PushEvent(fe);
          SpawnProjectile(i, heavy, me, aim);
          s.state.weapon = static_cast<uint8_t>(heavy);
          s.botFireDelay = 40;      // shoulder the rifle again afterwards
        }
      }

      // Fire when roughly on target. Skill tightens the cone they will open
      // up from, shortens the pause between bursts and tightens the spread.
      const float aimGate = 9.0f - s.botSkill * 5.0f;
      if (fabsf(diff) < aimGate && --s.botFireDelay <= 0) {
        const WeaponDef& d = Weapon(s.botArsenal.current);
        s.botFireDelay = d.cooldown +
                         GetRandomValue(0, 14 - (int)(s.botSkill * 12.0f));
        if (s.botArsenal.cur().mag <= 0) {
          s.botArsenal.cur().mag = d.magSize;   // bots never run dry
        } else {
          s.botArsenal.cur().mag--;
          // 4.2 degrees of cone at the bottom of the curve down to 1.1 at the
          // top -- enough that early waves are survivable in the open and
          // late ones are not.
          const float spread = (4.2f - s.botSkill * 3.1f) * kSpreadScale;
          const float yj = s.state.yaw + RandRange(-spread, spread);
          const float pj = s.state.pitch + RandRange(-spread, spread);
          const Vector3 dir = ForwardFromAngles(yj, pj);

          NetEvent fe;
          fe.type = EV_FIRE;
          fe.a = static_cast<uint8_t>(i);
          fe.weapon = s.botArsenal.current;
          fe.x = me.x; fe.y = me.y; fe.z = me.z;
          fe.dx = dir.x; fe.dy = dir.y; fe.dz = dir.z;
          PushEvent(fe);

          // Server-side hitscan for the bot.
          float bestT = d.range;
          int hitIdx = -1;
          bool head = false;
          if (world_) {
            const RayHit wh = world_->Raycast(me, dir, d.range);
            if (wh.hit) bestT = wh.dist;
          }
          for (int j = 0; j < kMaxPlayers; ++j) {
            if (j == i || !slots_[j].used || slots_[j].state.dead()) continue;
            // ...and their rounds pass through each other too, so a squad
            // does not thin itself out by shooting through its own front
            // rank.
            if (slots_[j].bot) continue;
            const PlayerState& ps = slots_[j].state;
            const Vector3 base = ps.pos();
            const float hgt = ps.height();
            // Ray vs vertical cylinder.
            const Vector3 rel = Vector3Subtract(me, base);
            const float a2 = dir.x * dir.x + dir.z * dir.z;
            if (a2 < 0.00001f) continue;
            const float b2 = 2.0f * (rel.x * dir.x + rel.z * dir.z);
            const float c2 = rel.x * rel.x + rel.z * rel.z - kPlayerRadius * kPlayerRadius;
            const float disc = b2 * b2 - 4.0f * a2 * c2;
            if (disc < 0.0f) continue;
            const float sq = sqrtf(disc);
            float tt = (-b2 - sq) / (2.0f * a2);
            if (tt < 0.0f) tt = (-b2 + sq) / (2.0f * a2);
            if (tt < 0.0f || tt >= bestT) continue;
            const float hy = me.y + dir.y * tt - base.y;
            if (hy < 0.0f || hy > hgt) continue;
            bestT = tt;
            hitIdx = j;
            head = hy > hgt * kHeadZone;
          }
          if (hitIdx >= 0) {
            float dmg = d.damage * (head ? kHeadMult : 1.0f);
            ServerSlot& v = slots_[hitIdx];
            if (v.state.armor > 0.0f) {
              const float ab = fminf(dmg * 0.5f, v.state.armor);
              v.state.armor -= ab;
              dmg -= ab;
            }
            v.state.health -= dmg;
            NetEvent hv;
            hv.type = EV_HIT;
            hv.a = static_cast<uint8_t>(i);
            hv.b = static_cast<uint8_t>(hitIdx);
            hv.weapon = s.botArsenal.current;
            hv.flags = head ? 1 : 0;
            hv.value = dmg;
            const Vector3 hp = Vector3Add(me, Vector3Scale(dir, bestT));
            hv.x = hp.x; hv.y = hp.y; hv.z = hp.z;
            PushEvent(hv);
            if (v.state.health <= 0.0f) KillPlayer(hitIdx, i, s.botArsenal.current, head);
          }
        }
      }
    } else {
      // Wander.
      if (GetRandomValue(0, 90) == 0) s.state.yaw += RandRange(-70.0f, 70.0f);
      wish = Vector3Scale(FlatForward(s.state.yaw), 1.8f);
      s.state.pitch *= 0.9f;
    }

    // ------------------------------------------------ move the bot
    if (world_) {
      Vector3 p = s.state.pos();
      bool blocked = false;
      p = world_->SlideMove(p, wish, kPlayerRadius, kPlayerHeight, &blocked);
      if (blocked && s.botOnGround && GetRandomValue(0, 3) == 0) s.botVy = kJumpSpeed;
      if (blocked) s.state.yaw += RandRange(-40.0f, 40.0f);

      const float prevY = p.y;
      p.y += s.botVy;
      s.botVy -= kGravity;
      const float g = world_->GroundHeight(p.x, p.z, kPlayerRadius, prevY + kStepHeight);
      if (p.y <= g) { p.y = g; s.botVy = 0.0f; s.botOnGround = true; }
      else s.botOnGround = false;
      p.x = Clampf(p.x, kPlayerRadius, world_->sizeX() - kPlayerRadius);
      p.z = Clampf(p.z, kPlayerRadius, world_->sizeZ() - kPlayerRadius);
      s.state.setPos(p);
      s.state.flags = s.botOnGround ? PF_ONGROUND : 0;
    }
  }
}

void Server::HandlePacket(const Endpoint& from, const uint8_t* data, int len,
                          double now) {
  Reader r(data, len);
  if (r.u32() != kProtocolId) return;
  const uint8_t type = r.u8();

  int slot = FindSlot(from);

  switch (type) {
    case MSG_JOIN: {
      if (slot < 0) {
        slot = AllocSlot();
        if (slot < 0) {
          Writer w;
          w.u32(kProtocolId); w.u8(MSG_REJECT); w.str("server full", 32);
          socket_.Send(from, w.buf, w.len);
          return;
        }
        slots_[slot] = ServerSlot{};
        slots_[slot].used = true;
        slots_[slot].addr = from;
        slots_[slot].state = PlayerState{};
        slots_[slot].state.id = static_cast<uint8_t>(slot);
        slots_[slot].state.active = 1;
        RespawnPlayer(slot);
        BroadcastRoster();
        TraceLog(LOG_INFO, "SERVER: %s joined as slot %d", from.ToString().c_str(), slot);
      }
      const std::string nm = r.str(24);
      snprintf(slots_[slot].state.name, sizeof(slots_[slot].state.name), "%s",
               nm.empty() ? "player" : nm.c_str());
      slots_[slot].lastRecv = now;

      Writer w;
      w.u32(kProtocolId); w.u8(MSG_ACCEPT);
      w.u8(static_cast<uint8_t>(slot));
      w.str(mapName_, 32);
      socket_.Send(from, w.buf, w.len);
      BroadcastRoster();
      break;
    }
    case MSG_INPUT: {
      if (slot < 0) return;
      ServerSlot& s = slots_[slot];
      s.lastRecv = now;
      const float x = r.f32(), y = r.f32(), z = r.f32();
      const float yaw = r.f32(), pitch = r.f32();
      const uint8_t weapon = r.u8();
      const uint8_t flags = r.u8();
      if (!s.state.dead()) {
        s.state.x = x; s.state.y = y; s.state.z = z;
        s.state.yaw = yaw; s.state.pitch = pitch;
        s.state.weapon = weapon;
        // The server owns the dead bit; the client owns posture/action bits.
        s.state.flags = (s.state.flags & PF_DEAD) |
                        (flags & (PF_CROUCH | PF_ONGROUND | PF_FIRING | PF_RELOAD | PF_ZOOM));
      }
      break;
    }
    case MSG_FIRE: {
      if (slot < 0 || slots_[slot].state.dead()) return;
      const uint8_t weapon = r.u8();
      const Vector3 o{r.f32(), r.f32(), r.f32()};
      const Vector3 d{r.f32(), r.f32(), r.f32()};
      const Vector3 extra{r.f32(), r.f32(), r.f32()};

      NetEvent ev;
      ev.type = EV_FIRE;
      ev.a = static_cast<uint8_t>(slot);
      ev.weapon = weapon;
      ev.x = o.x; ev.y = o.y; ev.z = o.z;
      ev.dx = d.x; ev.dy = d.y; ev.dz = d.z;
      PushEvent(ev);

      const WeaponDef& def = Weapon(weapon);
      if (def.mode == FIRE_PROJECTILE) SpawnProjectile(slot, weapon, o, d);
      else if (def.mode == FIRE_PLACE) PlaceCharge(slot, weapon, o, d, extra);
      break;
    }
    case MSG_FALL: {
      if (slot < 0 || slots_[slot].state.dead()) return;
      float dmg = r.f32();
      if (!(dmg > 0.0f)) return;                 // also rejects NaN
      // Capped at twice a full health bar: enough to be fatal from any
      // height, small enough that a bad packet cannot do anything strange.
      dmg = fminf(dmg, kMaxHealth * 2.0f);
      ServerSlot& s = slots_[slot];
      // Body armour is no help against the ground.
      s.state.health -= dmg;
      s.lastHitDir = Vector3{0.0f, 1.0f, 0.0f};
      s.lastHitHeight = 0.12f;                   // it lands on the legs
      NetEvent hv;
      hv.type = EV_HIT;
      hv.a = static_cast<uint8_t>(slot);
      hv.b = static_cast<uint8_t>(slot);
      hv.weapon = kDeathByFalling;
      hv.value = dmg;
      hv.x = s.state.x; hv.y = s.state.y; hv.z = s.state.z;
      PushEvent(hv);
      if (s.state.health <= 0.0f)
        KillPlayer(slot, slot, kDeathByFalling, false);
      break;
    }
    case MSG_HIT: {
      if (slot < 0 || slots_[slot].state.dead()) return;
      const uint8_t target = r.u8();
      const uint8_t weapon = r.u8();
      const uint8_t flags = r.u8();
      float dmg = r.f32();
      if (target >= kMaxPlayers || !slots_[target].used) return;
      ServerSlot& v = slots_[target];
      if (v.state.dead() || target == slot) return;

      // Sanity: never accept more than the weapon could plausibly do.
      const WeaponDef& d = Weapon(weapon);
      const float cap = d.damage * kHeadMult * (d.pellets > 1 ? d.pellets : 1) + 1.0f;
      if (dmg <= 0.0f || dmg > cap) return;
      if (Vector3Distance(slots_[slot].state.pos(), v.state.pos()) > d.range + 120.0f)
        return;

      if (v.state.armor > 0.0f) {
        const float ab = fminf(dmg * 0.5f, v.state.armor);
        v.state.armor -= ab;
        dmg -= ab;
      }
      v.state.health -= dmg;

      // Remember where this came from for the death throw.
      {
        Vector3 d = Vector3Subtract(v.state.pos(), slots_[slot].state.pos());
        d.y = 0.0f;
        v.lastHitDir = (Vector3LengthSqr(d) > 0.01f) ? Vector3Normalize(d)
                                                     : Vector3{0, 0, 1};
        v.lastHitDir.y = 0.18f;
        v.lastHitDir = Vector3Normalize(v.lastHitDir);
        v.lastHitHeight = (flags & 1) ? 0.88f : 0.62f;
      }

      NetEvent hv;
      hv.type = EV_HIT;
      hv.a = static_cast<uint8_t>(slot);
      hv.b = target;
      hv.weapon = weapon;
      hv.flags = flags;
      hv.value = dmg;
      hv.x = v.state.x; hv.y = v.state.y + v.state.height() * 0.6f; hv.z = v.state.z;
      hv.dx = v.lastHitDir.x; hv.dy = v.lastHitDir.y; hv.dz = v.lastHitDir.z;
      PushEvent(hv);

      if (v.state.health <= 0.0f)
        KillPlayer(target, slot, weapon, (flags & 1) != 0);
      break;
    }
    case MSG_PING: {
      const uint32_t stamp = r.u32();
      Writer w;
      w.u32(kProtocolId); w.u8(MSG_PONG); w.u32(stamp);
      socket_.Send(from, w.buf, w.len);
      break;
    }
    case MSG_LEAVE: {
      if (slot >= 0) {
        TraceLog(LOG_INFO, "SERVER: slot %d left", slot);
        slots_[slot] = ServerSlot{};
        BroadcastRoster();
      }
      break;
    }
    default:
      break;
  }
}

void Server::BroadcastSnapshot() {
  Writer w;
  w.u32(kProtocolId);
  w.u8(MSG_SNAPSHOT);
  w.u32(tick_);

  int count = 0;
  for (const ServerSlot& s : slots_) if (s.used) ++count;
  w.u8(static_cast<uint8_t>(count));
  for (const ServerSlot& s : slots_) {
    if (!s.used) continue;
    WirePlayer p{};
    p.id = s.state.id;
    p.team = s.state.team;
    p.weapon = s.state.weapon;
    p.flags = s.state.flags;
    p.kills = s.state.kills;
    p.deaths = s.state.deaths;
    p.x = s.state.x; p.y = s.state.y; p.z = s.state.z;
    p.yaw = s.state.yaw; p.pitch = s.state.pitch;
    p.health = s.state.health; p.armor = s.state.armor;
    w.raw(&p, sizeof(p));
  }

  const int nents = (int)ents_.size() < kMaxNetEnts ? (int)ents_.size() : kMaxNetEnts;
  w.u8(static_cast<uint8_t>(nents));
  for (int i = 0; i < nents; ++i) {
    const SimEntity& e = ents_[i];
    WireEntity we{};
    we.id = e.id; we.kind = e.kind; we.owner = e.owner;
    we.x = e.pos.x; we.y = e.pos.y; we.z = e.pos.z;
    if (e.kind == ENT_SMOKE) {
      // A canister carries its own clock instead of a velocity: the client
      // needs to know how far through venting it is to feed the cloud and to
      // fade it out, and once it has popped it is not going anywhere. Without
      // this the client saw arm == 0 for every canister and never vented at
      // all.
      we.ex = static_cast<float>(e.arm);
      we.ey = e.vel.y; we.ez = 0.0f;
    } else if (e.kind == ENT_ROCKET || e.kind == ENT_GRENADE) {
      we.ex = e.vel.x; we.ey = e.vel.y; we.ez = e.vel.z;
    } else {
      we.ex = e.beamEnd.x; we.ey = e.beamEnd.y; we.ez = e.beamEnd.z;
    }
    w.raw(&we, sizeof(we));
  }

  for (const ServerSlot& s : slots_)
    if (s.used && !s.bot) socket_.Send(s.addr, w.buf, w.len);
}

void Server::BroadcastRoster() {
  Writer w;
  w.u32(kProtocolId);
  w.u8(MSG_ROSTER);
  int count = 0;
  for (const ServerSlot& s : slots_) if (s.used) ++count;
  w.u8(static_cast<uint8_t>(count));
  for (const ServerSlot& s : slots_) {
    if (!s.used) continue;
    w.u8(s.state.id);
    w.u8(s.state.team);
    w.str(s.state.name, 24);
  }
  for (const ServerSlot& s : slots_)
    if (s.used && !s.bot) socket_.Send(s.addr, w.buf, w.len);
}

void Server::BroadcastEvents() {
  if (events_.empty()) return;
  // Resend a sliding window; clients discard anything they have already seen.
  const int n = (int)events_.size() < 20 ? (int)events_.size() : 20;
  Writer w;
  w.u32(kProtocolId);
  w.u8(MSG_EVENTS);
  w.u8(static_cast<uint8_t>(n));
  for (int i = (int)events_.size() - n; i < (int)events_.size(); ++i)
    WriteEvent(w, events_[i]);
  for (const ServerSlot& s : slots_)
    if (s.used && !s.bot) socket_.Send(s.addr, w.buf, w.len);
}

void Server::Tick(double now) {
  if (!running_) return;

  uint8_t buf[kMaxPacket];
  Endpoint from;
  int n;
  while ((n = socket_.Recv(buf, sizeof(buf), &from)) > 0) HandlePacket(from, buf, n, now);

  // Drop silent clients.
  for (int i = 0; i < kMaxPlayers; ++i) {
    ServerSlot& s = slots_[i];
    if (!s.used || s.bot) continue;
    if (s.lastRecv > 0.0 && now - s.lastRecv > kTimeout) {
      TraceLog(LOG_INFO, "SERVER: slot %d timed out", i);
      slots_[i] = ServerSlot{};
      BroadcastRoster();
    }
  }

  UpdateBots();
  SimulateEntities();

  for (int i = 0; i < kMaxPlayers; ++i) {
    ServerSlot& s = slots_[i];
    if (!s.used) continue;
    // Wave enemies stay dead: their slots are recycled by SetBotPopulation as
    // reinforcements arrive, which is what makes a wave finite.
    if (s.bot && !botRespawn_) continue;
    if (s.state.dead() && --s.respawnTimer <= 0) RespawnPlayer(i);
  }

  ++tick_;
  if (tick_ % kSnapshotRate == 0) {
    BroadcastSnapshot();
    BroadcastEvents();
  }
}

// ------------------------------------------------------------------ client

Vector3 RemotePlayer::Interp(double renderTime, float* outYaw,
                             float* outPitch) const {
  if (curTime <= prevTime) {
    if (outYaw) *outYaw = cur.yaw;
    if (outPitch) *outPitch = cur.pitch;
    return cur.pos();
  }
  double t = (renderTime - prevTime) / (curTime - prevTime);
  t = t < 0.0 ? 0.0 : (t > 1.6 ? 1.6 : t);   // allow a little extrapolation
  const float f = static_cast<float>(t);

  if (outYaw) {
    float d = cur.yaw - prev.yaw;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    *outYaw = prev.yaw + d * f;
  }
  if (outPitch) *outPitch = prev.pitch + (cur.pitch - prev.pitch) * f;
  return Vector3Lerp(prev.pos(), cur.pos(), f);
}

bool Client::Connect(const std::string& host, uint16_t port,
                     const std::string& name) {
  if (!NetInit()) { error_ = "winsock init failed"; return false; }
  if (!socket_.Open(0)) { error_ = "could not open a UDP socket"; return false; }
  if (!ResolveHost(host, port, &server_)) {
    error_ = "could not resolve " + host;
    return false;
  }
  name_ = name;
  state_ = CONNECTING;
  lastJoinSend_ = 0.0;
  myId_ = -1;
  lastEventSeq_ = 0;
  for (RemotePlayer& p : players_) p = RemotePlayer{};
  return true;
}

void Client::Disconnect() {
  if (state_ != IDLE) {
    Writer w;
    w.u32(kProtocolId); w.u8(MSG_LEAVE);
    socket_.Send(server_, w.buf, w.len);
  }
  socket_.Close();
  state_ = IDLE;
  myId_ = -1;
}

void Client::SendInput(const LocalPlayer& lp, uint32_t tick) {
  if (state_ != CONNECTED) return;
  uint8_t flags = 0;
  if (lp.crouch) flags |= PF_CROUCH;
  if (lp.onGround) flags |= PF_ONGROUND;
  if (lp.zoomed) flags |= PF_ZOOM;
  if (lp.arsenal.cur().reloading) flags |= PF_RELOAD;

  Writer w;
  w.u32(kProtocolId); w.u8(MSG_INPUT);
  w.f32(lp.pos.x); w.f32(lp.pos.y); w.f32(lp.pos.z);
  w.f32(lp.yaw); w.f32(lp.viewPitch());
  w.u8(lp.arsenal.current);
  w.u8(flags);
  socket_.Send(server_, w.buf, w.len);
}

void Client::SendHit(uint8_t target, float damage, uint8_t weapon, bool headshot) {
  if (state_ != CONNECTED) return;
  Writer w;
  w.u32(kProtocolId); w.u8(MSG_HIT);
  w.u8(target); w.u8(weapon); w.u8(headshot ? 1 : 0);
  w.f32(damage);
  socket_.Send(server_, w.buf, w.len);
}

void Client::SendFall(float damage) {
  if (state_ != CONNECTED) return;
  Writer w;
  w.u32(kProtocolId); w.u8(MSG_FALL);
  w.f32(damage);
  socket_.Send(server_, w.buf, w.len);
}

void Client::SendFire(uint8_t weapon, Vector3 origin, Vector3 dir, Vector3 extra) {
  if (state_ != CONNECTED) return;
  Writer w;
  w.u32(kProtocolId); w.u8(MSG_FIRE);
  w.u8(weapon);
  w.f32(origin.x); w.f32(origin.y); w.f32(origin.z);
  w.f32(dir.x); w.f32(dir.y); w.f32(dir.z);
  w.f32(extra.x); w.f32(extra.y); w.f32(extra.z);
  socket_.Send(server_, w.buf, w.len);
}

std::vector<NetEvent> Client::TakeEvents() {
  std::vector<NetEvent> out;
  out.swap(pending_);
  return out;
}

void Client::HandlePacket(const uint8_t* data, int len, double now) {
  Reader r(data, len);
  if (r.u32() != kProtocolId) return;
  const uint8_t type = r.u8();
  lastRecv_ = now;

  switch (type) {
    case MSG_ACCEPT: {
      myId_ = r.u8();
      state_ = CONNECTED;
      TraceLog(LOG_INFO, "CLIENT: connected as player %d", myId_);
      break;
    }
    case MSG_REJECT: {
      error_ = r.str(32);
      state_ = IDLE;
      break;
    }
    case MSG_ROSTER: {
      const int n = r.u8();
      bool seen[kMaxPlayers] = {false};
      for (int i = 0; i < n; ++i) {
        const int id = r.u8();
        const int team = r.u8();
        const std::string nm = r.str(24);
        if (id < 0 || id >= kMaxPlayers) continue;
        players_[id].name = nm;
        players_[id].cur.team = static_cast<uint8_t>(team);
        players_[id].prev.team = static_cast<uint8_t>(team);
        seen[id] = true;
      }
      for (int i = 0; i < kMaxPlayers; ++i)
        if (!seen[i]) players_[i].active = false;
      break;
    }
    case MSG_SNAPSHOT: {
      r.u32();   // server tick
      const int n = r.u8();
      bool seen[kMaxPlayers] = {false};
      for (int i = 0; i < n; ++i) {
        WirePlayer p{};
        r.raw(&p, sizeof(p));
        if (p.id >= kMaxPlayers) continue;
        RemotePlayer& rp = players_[p.id];
        rp.prev = rp.cur;
        rp.prevTime = rp.curTime;
        rp.cur.id = p.id;
        rp.cur.active = 1;
        rp.cur.team = p.team;
        rp.cur.weapon = p.weapon;
        rp.cur.flags = p.flags;
        rp.cur.kills = p.kills;
        rp.cur.deaths = p.deaths;
        rp.cur.x = p.x; rp.cur.y = p.y; rp.cur.z = p.z;
        rp.cur.yaw = p.yaw; rp.cur.pitch = p.pitch;
        rp.cur.health = p.health; rp.cur.armor = p.armor;
        rp.curTime = now;
        if (!rp.active) { rp.prev = rp.cur; rp.prevTime = now - 0.05; }
        rp.active = true;
        seen[p.id] = true;
      }
      for (int i = 0; i < kMaxPlayers; ++i)
        if (!seen[i]) players_[i].active = false;

      const int ne = r.u8();
      ents_.clear();
      for (int i = 0; i < ne; ++i) {
        WireEntity we{};
        r.raw(&we, sizeof(we));
        SimEntity e;
        e.id = we.id; e.kind = we.kind; e.owner = we.owner;
        e.pos = Vector3{we.x, we.y, we.z};
        if (e.kind == ENT_SMOKE) {
          e.arm = static_cast<int>(we.ex);
          e.vel = Vector3{0.0f, we.ey, 0.0f};
        } else if (e.kind == ENT_ROCKET || e.kind == ENT_GRENADE) {
          e.vel = Vector3{we.ex, we.ey, we.ez};
        } else {
          e.beamEnd = Vector3{we.ex, we.ey, we.ez};
        }
        ents_.push_back(e);
      }
      break;
    }
    case MSG_EVENTS: {
      const int n = r.u8();
      uint32_t highest = lastEventSeq_;
      for (int i = 0; i < n; ++i) {
        const NetEvent e = ReadEvent(r);
        if (e.seq > lastEventSeq_) {
          pending_.push_back(e);
          if (e.seq > highest) highest = e.seq;
        }
      }
      lastEventSeq_ = highest;
      break;
    }
    case MSG_PONG: {
      const uint32_t stamp = r.u32();
      const double sent = static_cast<double>(stamp) / 1000.0;
      pingMs_ = static_cast<float>((now - sent) * 1000.0);
      break;
    }
    default:
      break;
  }
}

void Client::Pump(double now) {
  if (state_ == IDLE) return;

  if (state_ == CONNECTING && now - lastJoinSend_ > 0.4) {
    lastJoinSend_ = now;
    Writer w;
    w.u32(kProtocolId); w.u8(MSG_JOIN); w.str(name_, 24);
    socket_.Send(server_, w.buf, w.len);
  }

  uint8_t buf[kMaxPacket];
  int n;
  while ((n = socket_.Recv(buf, sizeof(buf), nullptr)) > 0)
    HandlePacket(buf, n, now);

  if (state_ == CONNECTED && now - lastPing_ > 1.0) {
    lastPing_ = now;
    Writer w;
    w.u32(kProtocolId); w.u8(MSG_PING);
    w.u32(static_cast<uint32_t>(now * 1000.0));
    socket_.Send(server_, w.buf, w.len);
  }
}

}  // namespace kaj
