// Top level: owns everything, runs the fixed 60 Hz tick and draws the frame.
//
// Firing is handled at *frame* rate rather than tick rate so a click fires on
// the very frame it happens; movement and everything else stays on the fixed
// 60 Hz tick that GTJ3D's constants assume.
#pragma once

#include <deque>
#include <string>

#include "assets.h"
#include "common.h"
#include "entities.h"
#include "fx.h"
#include "net.h"
#include "player.h"
#include "render.h"
#include "sky.h"
#include "story.h"
#include "vehicle.h"
#include "world.h"

namespace kaj {

struct LaunchOptions {
  bool host = true;
  bool singlePlayer = false;
  std::string connectTo = "127.0.0.1";
  uint16_t port = kDefaultPort;
  std::string name = "Kaj";
  int bots = 3;
  // The dev test range is the default: an open plain with one of every
  // vehicle and a gun range. Pick anything else from the menu's MAP row.
  std::string map = "devtest";
  bool skipMenu = false;
  // Story mode is single player with the wave campaign running on top.
  bool story = false;
  // Start the campaign partway up the curve. 0 means wave one. Exists for the
  // screenshot tests, which need to reach the armour and the gunships without
  // playing six waves first.
  int storyWave = 0;
};

// The maps shipped in assets/maps. The menu cycles through these.
inline const char* const* MapList(int* count) {
  static const char* kMaps[] = {"devtest", "urban", "gtj"};
  *count = 3;
  return kMaps;
}

// Human-readable name for the map picker.
inline const char* MapTitle(const char* key) {
  struct Row { const char* key; const char* title; };
  static const Row kRows[] = {
      {"devtest",  "Dev Test Range"},
      {"urban",    "Urban Complex"},
      {"gtj",      "Grand Theft Jack"},
  };
  for (const Row& r : kRows)
    if (std::string(key) == r.key) return r.title;
  return key;
}

// A round in flight, tracked purely so its audio lands at the right moment.
// The whoosh only fires if the bullet actually reaches its closest approach to
// the listener; if it buries itself in a wall first, the impact plays instead
// and the whoosh never happens.
struct BulletTrace {
  Vector3 origin{}, dir{};
  float speed = 9000.0f;
  float travelled = 0.0f;
  float hitDist = 0.0f;      // where it stops: geometry, a player, or max range
  float missAt = 0.0f;       // travel distance at closest approach to the ear
  float missDist = 1e9f;
  Vector3 hitPoint{}, hitNormal{0, 1, 0};
  int brushIndex = -1;
  bool hitWorld = false;
  bool playedWhoosh = false;
  bool selfShot = false;     // our own rounds never whoosh at us
  uint8_t weapon = 0;
};

class Game {
 public:
  bool Init(const LaunchOptions& opts);
  void Shutdown();
  void Frame();
  bool wantsQuit() const { return quit_; }

  // Development aids used by tools/smoketest.ps1 and tools/mptest.ps1.
  void SetDebugYaw(float yaw) { debugYaw_ = yaw; }
  void SetDebugPitch(float p) { debugPitch_ = p; }
  void SetDebugScores(bool on) { debugScores_ = on; }
  void SetDebugTeleport(Vector3 p) { debugTeleport_ = p; debugHasTeleport_ = true; }
  void SetDebugPin(bool on) { debugPin_ = on; }
  void SetDebugFire(int weapon) { debugFireWeapon_ = weapon; }
  void SetDebugWalk(bool on) { debugWalk_ = on; }
  // Gets into the nearest vehicle and drives forward, for automated runs.
  void SetDebugDrive(bool on) { debugDrive_ = on; }
  // Heartbeat logging for automated runs, so a stall can be pinned to a phase.
  void SetDebugTrace(bool on) { debugTrace_ = on; }
  // Parks a static enemy in front of the camera so the model can be inspected.
  void SetDebugShowcase(bool on) { debugShowcase_ = on; }
  void SetWeather(WeatherPreset p) { sky_.SetPreset(p); }
  void DebugReport() const;

 private:
  enum class Mode { Menu, Connecting, Playing, Error };

  // --- flow ---------------------------------------------------------------
  void StartHost();
  void StartJoin();
  void StartSinglePlayer();
  void StartStory();
  void LeaveGame();

  // --- story mode ---------------------------------------------------------
  // One tick of the campaign: feed the wave state machine, top the map up
  // with reinforcements, put the wave's armour on the ground and turn any
  // hostile vehicle's trigger pull into a tracer, a blast and damage.
  void TickStory();
  void SpawnWaveArmour();
  void ResolveHostileFire();
  // A stopped SWAT van empties its squad into the street.
  void DeploySwatSquads();
  // Hostiles still standing: bots plus armour. What decides a wave is over.
  int EnemiesAlive() const;

  // --- simulation ---------------------------------------------------------
  void Tick();
  void GatherInput(InputCommand* cmd);
  void HandleMouseLook(float dt);
  void HandleDevKeys();
  void TryFire(bool pressed, bool held);
  void DoHitscan();
  void DoMelee();
  void DoPlace();
  void ProcessEvents();

  // --- helpers ------------------------------------------------------------
  int TracePlayers(Vector3 origin, Vector3 dir, float maxDist, float* outT,
                   bool* outHead, bool* outLeg) const;
  Camera3D BuildCamera() const;
  // Where the drawn viewmodel's barrel actually is, so tracers and barrel
  // smoke start there rather than at the bridge of the player's nose.
  Vector3 MuzzlePos() const;
  // Registers a round so its fly-by and impact audio play as it travels.
  void TrackBullet(Vector3 from, Vector3 dir, float stopDist, bool hitWorld,
                   Vector3 hitPoint, Vector3 hitNormal, int brushIndex,
                   uint8_t weapon, bool selfShot);
  void UpdateBullets(float dt);
  // Rockets and grenades in flight go past your ear too. Bullets get their
  // whoosh from BulletTrace, which is a client-side prediction of a hitscan;
  // a projectile is a server-simulated entity with a real position, so its
  // near-miss is measured off that instead -- it fires on the frame the thing
  // is at its closest and starting to recede.
  void UpdateProjectileFlyBys();
  // Feeds the cloud from every popped smoke canister, every frame it is
  // venting. Emitting one big puff at pop time gives a static ball that
  // blinks out; a continuous feed billows and drifts.
  void UpdateSmoke(float dt);
  // `inDir` is the direction the round was travelling, so the mark it leaves
  // can follow the round in rather than always being a circle stamped square
  // on the wall.
  void SurfaceImpact(Vector3 point, Vector3 normal, int brushIndex, bool loud,
                     Vector3 inDir = Vector3{0, 0, 0});
  // Throws blood off a wound and lets it land on whatever is behind it. Each
  // spot is a separate cast in its own direction, so the splatter follows the
  // walls, floors and ceilings it actually reaches instead of being a decal
  // stamped under the body. `amount` scales the number of casts and their
  // size -- a graze throws two or three, a death throws a dozen.
  void SplatterBlood(Vector3 wound, Vector3 shotDir, float amount);
  // Reload sound schedule, driven off the GTJ3D clips.
  void UpdateReloadAudio();
  // Getting in, driving and getting out. Runs on the 60 Hz tick so the GTJ3D
  // per-step handling constants mean what they meant in GTJ3D.
  void TickVehicles(const InputCommand& in);
  // --- tank gunnery -------------------------------------------------------
  // A tank does not use the player's arsenal: it has its own two weapons, its
  // own ammo and its own cooldowns, and it aims with the turret rather than
  // with the hull.
  bool InTank() const;
  bool InHeli() const;
  void TryFireTank(bool pressed, bool held);
  void TryFireHeli(bool pressed, bool held);
  Vector3 FindExitSpot(const Vehicle& v) const;
  void ApplyLocalBlast(Vector3 at, float radius, float damage);
  static bool HurtsTank(int weapon);
  void AbsorbVehicleDamage(float amount, int weapon);
  const Texture2D* SkinFor(int playerId) const;
  void PushKillFeed(const std::string& s);
  void SetMessage(const std::string& s, float seconds);

  // --- draw ---------------------------------------------------------------
  void DrawGame();
  void DrawMenu();
  void DrawDevOverlay();

  LaunchOptions opts_;
  Mode mode_ = Mode::Menu;
  bool quit_ = false;
  bool singlePlayer_ = false;
  std::string errorText_;

  Assets assets_;
  World world_;
  Renderer renderer_;
  FxSystem fx_;
  Sky sky_;
  Story story_;
  bool storyMode_ = false;
  VehicleSystem vehicles_;
  int drivingVehicle_ = -1;   // index into vehicles_, -1 = on foot
  int enterTimer_ = 0;        // GTJ's entering_car_timer: 60 ticks of door+start
  int nearVehicle_ = -1;      // what the "press E" prompt is pointing at
  int driveTurning_ = 0;      // GTJ's `turning`: 0 straight, 1 left, 2 right
  bool engineAudio_ = false;
  // A hostile gunship's rotor, heard from the ground. Separate from
  // engineAudio_, which is the machine you are sitting in.
  bool heliAmbient_ = false;
  bool driveThrottlePrev_ = false;

  // Tank armament. 1200 rounds per minute is one round every three ticks at
  // 60 Hz; the main gun takes two and a half seconds between shells.
  static constexpr int kTankCannonCooldown = 150;
  static constexpr int kTankMgCooldown = 3;
  static constexpr int kTankMgReload = 190;
  static constexpr int kTankMgMagSize = 2000;
  static constexpr float kTankCannonRange = 6000.0f;
  int tankWeapon_ = 0;        // 0 = main gun, 1 = roof machine gun
  int tankCooldown_ = 0;
  int tankReload_ = 0;
  int tankMgMag_ = kTankMgMagSize;
  // Gunship armament: a 5000-round belt at the same 1200 rpm, and ten rockets
  // in each of the two wing pods.
  static constexpr int kHeliBeltSize = 5000;
  static constexpr int kHeliRockets = 20;
  static constexpr int kHeliMgReload = 240;
  static constexpr int kHeliRocketCooldown = 26;
  static constexpr int kHeliRocketReload = 300;
  int heliBelt_ = kHeliBeltSize;
  int heliRockets_ = kHeliRockets;
  bool heliSide_ = false;     // which wing fires next
  uint32_t mgFireTick_ = 0;   // tick the last MG round left the barrel
  bool mgLoop_ = false;
  double lastRicochet_ = 0.0;
  // Server health last seen, so damage taken can be diverted into the vehicle.
  float lastServerHealth_ = kMaxHealth;
  int lastHitWeapon_ = WEAPON_RIFLE;
  Server server_;
  Client client_;
  LocalPlayer lp_;

  double accumulator_ = 0.0;
  uint32_t tick_ = 0;
  double now_ = 0.0;

  // Input / view
  float sensitivity_ = 6.0f;
  bool mouseCaptured_ = false;
  InputCommand frameInput_;
  bool firePressedLatch_ = false;
  // Weapon switching and reloading are handled at frame rate, not tick rate.
  // Edge-triggered keys only read true on the frame they are pressed, and at
  // 500 fps most frames run no 60 Hz tick at all -- so a press stored for the
  // tick to consume was usually overwritten and lost before any tick ran.
  void ApplyWeaponInput(const InputCommand& cmd);

  // Viewmodel animation
  float vmRecoilT_ = 0.0f;   // 1 on firing, decays; drives procedural kick
  int vmFrame_ = 0;
  float vmAnimT_ = 0.0f;
  float vmAnimDur_ = 0.0f;
  int vmAnimFrames_ = 1;

  // Short-lived point lights: a muzzle flash lights the wall beside you for a
  // frame or two, an explosion for rather longer.
  struct FlashLight {
    Vector3 pos;
    Vector3 color;
    float radius = 200.0f;
    float age = 0.0f, life = 0.06f;
  };
  std::vector<FlashLight> flashLights_;
  void AddFlashLight(Vector3 pos, Vector3 color, float radius, float life);

  std::vector<BulletTrace> bullets_;
  // Projectiles whose fly-by has already been played, and how close they were
  // last frame, keyed by entity id. Without the "already played" set a rocket
  // sitting near the closest-approach point would re-trigger every frame.
  struct FlyByTrack {
    uint16_t id = 0;
    float lastDist = 1e9f;
    bool played = false;
  };
  std::vector<FlyByTrack> flyBys_;
  float smokeEmit_ = 0.0f;   // accumulator for the smoke emission cadence
  // Reload audio bookkeeping: which weapon is reloading and how far through it
  // we were last frame, so each stage plays exactly once.
  int reloadAudioWeapon_ = -1;
  float reloadAudioPrev_ = 0.0f;
  int reloadShellCount_ = -1;

  // Feedback
  float hitMarker_ = 0.0f;
  bool hitWasHead_ = false;
  float damageFlash_ = 0.0f;
  float shake_ = 0.0f;
  float respawnIn_ = 0.0f;
  std::string message_;
  float messageTime_ = 0.0f;
  std::deque<std::string> killFeed_;
  bool showScores_ = false;

  // Dev tools
  bool devOverlay_ = false;
  bool devNoclip_ = false;
  bool devGodMode_ = false;
  bool devHelp_ = false;

  float debugYaw_ = 1e9f;
  float debugPitch_ = 1e9f;
  bool debugScores_ = false;
  Vector3 debugTeleport_{};
  bool debugHasTeleport_ = false;
  bool debugPin_ = false;
  int debugFireWeapon_ = -1;
  bool debugWalk_ = false;
  bool debugDrive_ = false;
  bool debugTrace_ = false;
  bool debugShowcase_ = false;
  int debugPhase_ = 0;
  double debugLastBeat_ = 0.0;
  long long debugFrames_ = 0;

  // Menu
  int menuIndex_ = 0;
  std::string ipBuffer_ = "127.0.0.1";
  bool editingIp_ = false;
};

}  // namespace kaj
