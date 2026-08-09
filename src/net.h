// UDP client/server for Gunlife.
//
// Model: movement is client-authoritative (each client reports where it is),
// hit registration is shooter-authoritative with server-side sanity checks, and
// everything explosive -- rockets, grenades, mines, smoke -- is simulated
// on the server and mirrored to clients. That keeps shooting as responsive as
// CS while leaving the server in charge of health, deaths and scores.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "common.h"
#include "entities.h"
#include "player.h"

namespace kaj {

class World;

constexpr uint16_t kDefaultPort = 27015;
constexpr uint32_t kProtocolId  = 0x4B414A35;  // 'KAJ5'
constexpr int kMaxPlayers   = 12;
constexpr int kMaxNetEnts   = 24;
constexpr int kSnapshotRate = 2;   // send a snapshot every N ticks (30 Hz)

enum MsgType : uint8_t {
  MSG_JOIN = 1,
  MSG_ACCEPT,
  MSG_REJECT,
  MSG_INPUT,
  MSG_SNAPSHOT,
  MSG_ROSTER,
  MSG_EVENTS,
  MSG_HIT,
  MSG_FIRE,
  MSG_LEAVE,
  MSG_PING,
  MSG_PONG,
  MSG_CHAT,
  // Damage you did to yourself by hitting the ground. MSG_HIT cannot carry
  // it: it rejects `target == slot` outright, and it caps damage at what the
  // named weapon could do, which no weapon-shaped cap fits.
  MSG_FALL,
};

// Stand-in weapon id on a death event, so the kill feed can tell a fall from
// a grenade you dropped at your own feet. Deliberately above WEAPON_COUNT.
constexpr uint8_t kDeathByFalling = 0xFE;

enum EventType : uint8_t {
  EV_FIRE = 1,   // someone pulled a trigger (tracer + sound)
  EV_BLAST,      // explosion at a point
  EV_DEATH,      // victim, killer, weapon
  EV_HIT,        // attacker landed damage on victim
  EV_SPAWN,      // player respawned
  EV_JOINLEAVE,  // roster changed
};

struct NetEvent {
  uint32_t seq = 0;
  uint8_t type = 0;
  uint8_t a = 0;       // shooter / victim
  uint8_t b = 0;       // killer / kind
  uint8_t weapon = 0;
  uint8_t flags = 0;   // bit0 = headshot
  float x = 0, y = 0, z = 0;
  float dx = 0, dy = 0, dz = 0;
  float value = 0;     // radius / damage
};

// Compact wire form of a player.
#pragma pack(push, 1)
struct WirePlayer {
  uint8_t id, team, weapon, flags;
  int16_t kills, deaths;
  float x, y, z, yaw, pitch, health, armor;
};
struct WireEntity {
  uint16_t id;
  uint8_t kind, owner;
  float x, y, z;
  // Multi-purpose: the current velocity for a rocket or grenade (clients need
  // it to orient the model), and for a smoke canister the ticks it has left to
  // vent, which is what tells the client how hard to feed the cloud.
  float ex, ey, ez;
};
#pragma pack(pop)

// ------------------------------------------------------------------- sockets
bool NetInit();
void NetShutdown();

struct Endpoint {
  uint32_t ip = 0;     // network byte order
  uint16_t port = 0;
  bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
  std::string ToString() const;
};

class UdpSocket {
 public:
  ~UdpSocket() { Close(); }
  bool Open(uint16_t port);       // port 0 = ephemeral
  void Close();
  bool Send(const Endpoint& to, const void* data, int len);
  int  Recv(void* buf, int maxLen, Endpoint* from);
  bool valid() const { return handle_ != ~0ull; }
  uint16_t localPort() const { return localPort_; }

 private:
  unsigned long long handle_ = ~0ull;
  uint16_t localPort_ = 0;
};

bool ResolveHost(const std::string& host, uint16_t port, Endpoint* out);

// -------------------------------------------------------------------- server
struct ServerSlot {
  bool used = false;
  bool bot = false;
  Endpoint addr;
  double lastRecv = 0.0;
  PlayerState state;
  int respawnTimer = 0;
  int lastHitTick = 0;
  // Direction and body height of the most recent damage, forwarded with the
  // death event so clients can throw the corpse the right way.
  Vector3 lastHitDir{0, 1, 0};
  float lastHitHeight = 0.6f;
  // Bot brain
  float botTargetYaw = 0.0f;
  int botThink = 0;
  int botTarget = -1;
  float botStrafe = 0.0f;
  int botFireDelay = 0;
  Arsenal botArsenal;
  float botVy = 0.0f;
  bool botOnGround = true;
  // Story mode: how good this one is (0 = wave one conscript, 1 = the ceiling)
  // and how much health it respawned with. Deathmatch bots sit at the middle
  // of the range, which is what they always effectively were.
  float botSkill = 0.5f;
  float botHealth = kMaxHealth;
  // Ticks until this one may reach for a grenade or a launcher again. Heavy
  // weapons are rate-limited per bot rather than by ammunition, so a squad
  // lays down the occasional rocket instead of a constant barrage.
  int botHeavyDelay = 0;
  // Only about one operator in five is carrying a launcher. Rolled once when
  // the slot is filled, so a squad is mostly rifles with the odd rocket
  // coming in rather than a firing line of launchers. The rest make do with
  // grenades.
  bool botHasLauncher = false;
};

class Server {
 public:
  bool Start(uint16_t port, const World* world, const std::string& mapName,
             int bots);
  void Stop();
  void Tick(double now);   // called at 60 Hz

  bool running() const { return running_; }
  uint16_t port() const { return socket_.localPort(); }
  int playerCount() const;

  // --- story mode ----------------------------------------------------------
  // Wave enemies are ordinary bots in ordinary player slots, so they shoot,
  // die, gib and show on the scoreboard exactly like deathmatch bots. Story
  // mode only needs to control how many there are, how good they are, and
  // that they stay dead.
  //
  // Adds or removes bots to reach `want` live ones. Returns how many were
  // added. Never touches a human slot.
  int SetBotPopulation(int want, float skill, float health);
  // Puts one bot on the ground at a given point rather than at a spawn, for
  // a SWAT van emptying its squad into the street. Returns false when there
  // is no free slot. `skill` and `health` match SetBotPopulation's.
  bool SpawnBotAt(Vector3 pos, float yawDeg, float skill, float health);
  // How many bots are alive right now. Dead-but-waiting-to-respawn do not
  // count, so a wave ends when the last one falls rather than when its body
  // finishes its respawn timer.
  int aliveBots() const;
  // Off for story mode: a wave enemy that goes down is gone, and its slot is
  // freed for the next reinforcement.
  void SetBotRespawn(bool on) { botRespawn_ = on; }

 private:
  void HandlePacket(const Endpoint& from, const uint8_t* data, int len, double now);
  int  FindSlot(const Endpoint& e) const;
  int  AllocSlot();
  void RespawnPlayer(int slot);
  void PushEvent(const NetEvent& e);
  void BroadcastSnapshot();
  void BroadcastRoster();
  void BroadcastEvents();
  void SpawnProjectile(int owner, int weapon, Vector3 origin, Vector3 dir);
  void PlaceCharge(int owner, int weapon, Vector3 pos, Vector3 normal, Vector3 beamEnd);
  void SimulateEntities();
  void Detonate(const SimEntity& e);
  void ApplyBlast(Vector3 pos, float radius, float damage, int owner, int weapon,
                  uint8_t kind);
  void KillPlayer(int victim, int killer, int weapon, bool headshot);
  void UpdateBots();

  UdpSocket socket_;
  const World* world_ = nullptr;
  std::string mapName_;
  bool running_ = false;
  uint32_t tick_ = 0;
  uint32_t eventSeq_ = 1;
  uint16_t nextEntId_ = 1;

  bool botRespawn_ = true;

  ServerSlot slots_[kMaxPlayers];
  std::vector<SimEntity> ents_;
  std::deque<NetEvent> events_;   // recent events, resent for reliability
};

// -------------------------------------------------------------------- client
struct RemotePlayer {
  bool active = false;
  PlayerState prev, cur;
  double prevTime = 0.0, curTime = 0.0;
  std::string name;

  // Smoothed render state.
  Vector3 Interp(double renderTime, float* outYaw, float* outPitch) const;
};

class Client {
 public:
  bool Connect(const std::string& host, uint16_t port, const std::string& name);
  void Disconnect();
  void Pump(double now);

  void SendInput(const LocalPlayer& lp, uint32_t tick);
  void SendHit(uint8_t target, float damage, uint8_t weapon, bool headshot);
  // Fall damage. The server applies it to whoever sent it.
  void SendFall(float damage);
  void SendFire(uint8_t weapon, Vector3 origin, Vector3 dir, Vector3 extra);

  bool connected() const { return state_ == CONNECTED; }
  bool connecting() const { return state_ == CONNECTING; }
  const std::string& error() const { return error_; }
  int myId() const { return myId_; }
  float ping() const { return pingMs_; }

  const RemotePlayer* players() const { return players_; }
  const std::vector<SimEntity>& entities() const { return ents_; }
  // Events received since the last call; the caller drains them.
  std::vector<NetEvent> TakeEvents();
  // Authoritative copy of our own record, refreshed by every snapshot.
  const PlayerState& self() const { return players_[myId_ < 0 ? 0 : myId_].cur; }
  bool haveSelf() const { return myId_ >= 0 && players_[myId_].active; }

  double serverTimeOffset() const { return timeOffset_; }

 private:
  enum State { IDLE, CONNECTING, CONNECTED } state_ = IDLE;
  void HandlePacket(const uint8_t* data, int len, double now);

  UdpSocket socket_;
  Endpoint server_;
  std::string name_;
  std::string error_;
  int myId_ = -1;
  double lastJoinSend_ = 0.0;
  double lastRecv_ = 0.0;
  double lastPing_ = 0.0;
  double timeOffset_ = 0.0;
  float pingMs_ = 0.0f;
  uint32_t lastEventSeq_ = 0;

  RemotePlayer players_[kMaxPlayers];
  std::vector<SimEntity> ents_;
  std::vector<NetEvent> pending_;
};

}  // namespace kaj
