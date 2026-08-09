#include "game.h"

#include <algorithm>
#include <cstdio>

#include "rlgl.h"

namespace kaj {
namespace {

// Ray vs an upright cylinder of radius r, base at `base`, height h.
bool RayCylinder(Vector3 o, Vector3 d, Vector3 base, float r, float h, float* outT,
                 float* outLocalY) {
  const Vector3 rel = Vector3Subtract(o, base);
  const float a = d.x * d.x + d.z * d.z;
  if (a < 1e-6f) return false;
  const float b = 2.0f * (rel.x * d.x + rel.z * d.z);
  const float c = rel.x * rel.x + rel.z * rel.z - r * r;
  const float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) return false;
  const float sq = sqrtf(disc);
  float t = (-b - sq) / (2.0f * a);
  if (t < 0.0f) t = (-b + sq) / (2.0f * a);
  if (t < 0.0f) return false;
  const float y = rel.y + d.y * t;
  if (y < 0.0f || y > h) return false;
  *outT = t;
  *outLocalY = y;
  return true;
}

// Closest approach of point p to segment a-b.
float SegmentDist(Vector3 p, Vector3 a, Vector3 b, Vector3* closest) {
  const Vector3 ab = Vector3Subtract(b, a);
  const float denom = Vector3DotProduct(ab, ab);
  if (denom < 1e-4f) { if (closest) *closest = a; return Vector3Distance(p, a); }
  const float t = Clampf(Vector3DotProduct(Vector3Subtract(p, a), ab) / denom, 0.0f, 1.0f);
  const Vector3 c = Vector3Add(a, Vector3Scale(ab, t));
  if (closest) *closest = c;
  return Vector3Distance(p, c);
}

std::string AssetRoot() {
  if (DirectoryExists("assets")) return "assets";
  if (DirectoryExists("../assets")) return "../assets";
  return "assets";
}

std::string DataPath(const std::string& rel) {
  std::string a = std::string("assets/") + rel;
  if (FileExists(a.c_str())) return a;
  std::string b = std::string("../assets/") + rel;
  if (FileExists(b.c_str())) return b;
  return a;
}

// Dust colour for the surface that was hit, so concrete and metal do not throw
// the same puff.
Color DustFor(const World& world, int brushIndex) {
  if (brushIndex < 0) return Color{132, 128, 120, 255};   // street
  const std::string& t = world.brushes()[brushIndex].tex;
  if (t == "metal" || t == "metal_panel" || t == "metal_panel2" ||
      t == "metal_door" || t == "grate" || t == "grate2")
    return Color{178, 180, 186, 255};
  if (t == "wood" || t == "crate" || t == "crate2" || t == "door")
    return Color{146, 116, 76, 255};
  return Color{158, 150, 138, 255};
}

bool IsMetal(const World& world, int brushIndex) {
  if (brushIndex < 0) return false;
  const std::string& t = world.brushes()[brushIndex].tex;
  return t == "metal" || t == "metal_panel" || t == "metal_panel2" ||
         t == "metal_door" || t == "grate" || t == "grate2" || t == "pillar";
}

}  // namespace

bool Game::Init(const LaunchOptions& opts) {
  opts_ = opts;
  const double t0 = GetTime();
  if (!assets_.Load(AssetRoot())) {
    errorText_ = "Could not load assets. Run tools/stage_assets.py first.";
    mode_ = Mode::Error;
  }
  const double t1 = GetTime();
  world_.Load(DataPath(("maps/" + opts_.map + ".map").c_str()));
  const double t2 = GetTime();
  vehicles_.Reset(world_);
  renderer_.Init();
  assets_.SetVehicleShader(renderer_.worldShader());
  const double t3 = GetTime();
  fx_.Init();
  const double t4 = GetTime();
  sky_.Init();
  const double t5 = GetTime();
  TraceLog(LOG_INFO,
           "INIT: assets %.2fs  map %.2fs  shaders %.2fs  fx %.2fs  sky %.2fs",
           t1 - t0, t2 - t1, t3 - t2, t4 - t3, t5 - t4);
  // The sky runs its ten-minute day and re-rolls the weather on the same
  // clock unless a fixed preset is asked for on the command line or with F7.
  sky_.SetPreset(WeatherPreset::Cycle);
  ipBuffer_ = opts.connectTo;

  if (opts.skipMenu) {
    if (opts.story) StartStory();
    else if (opts.singlePlayer) StartSinglePlayer();
    else if (opts.host) StartHost();
    else StartJoin();
  }
  return true;
}

void Game::Shutdown() {
  LeaveGame();
  sky_.Shutdown();
  fx_.Shutdown();
  renderer_.Shutdown();
  assets_.Unload();
  NetShutdown();
}

// ---------------------------------------------------------------------- flow

void Game::StartHost() {
  server_.Stop();
  client_.Disconnect();
  singlePlayer_ = false;
  if (!server_.Start(opts_.port, &world_, world_.name(), opts_.bots)) {
    errorText_ = "Could not open port " + std::to_string(opts_.port) +
                 " (already in use?)";
    mode_ = Mode::Error;
    return;
  }
  if (!client_.Connect("127.0.0.1", server_.port(), opts_.name)) {
    errorText_ = client_.error();
    mode_ = Mode::Error;
    return;
  }
  mode_ = Mode::Connecting;
}

void Game::StartJoin() {
  server_.Stop();
  client_.Disconnect();
  singlePlayer_ = false;
  if (!client_.Connect(ipBuffer_, opts_.port, opts_.name)) {
    errorText_ = client_.error();
    mode_ = Mode::Error;
    return;
  }
  mode_ = Mode::Connecting;
}

// Single player is the same match, hosted on an ephemeral loopback port so it
// never clashes with a real server and nobody can wander in. Handy for dev
// testing: it starts instantly, grabs the mouse, and opens the dev overlay.
void Game::StartSinglePlayer() {
  server_.Stop();
  client_.Disconnect();
  singlePlayer_ = true;
  const int bots = opts_.bots > 0 ? opts_.bots : 3;
  if (!server_.Start(0, &world_, world_.name(), bots)) {
    errorText_ = "Could not start the local server";
    mode_ = Mode::Error;
    return;
  }
  if (!client_.Connect("127.0.0.1", server_.port(), opts_.name)) {
    errorText_ = client_.error();
    mode_ = Mode::Error;
    return;
  }
  devOverlay_ = true;
  // Dev mode starts invulnerable so you can stand in a firefight and watch the
  // effects. Noclip stays off on purpose -- gravity, collision, step-up and
  // vehicles all still apply, which is the point of a test range.
  devGodMode_ = true;
  devNoclip_ = false;
  mode_ = Mode::Connecting;
}

// Story mode is single player with the campaign on top: same local server,
// same bot slots, same vehicles. The differences are that it starts empty
// (the waves fill it), enemies do not respawn, and you are not invulnerable.
void Game::StartStory() {
  server_.Stop();
  client_.Disconnect();
  singlePlayer_ = true;
  storyMode_ = true;
  // The dev range is a flat plain with a gun line on it -- fine for testing a
  // weapon, no use at all for a campaign, and it is the default map. Anything
  // deliberately chosen from the MAP row is left alone.
  if (opts_.map == "devtest") {
    opts_.map = "urban";
    world_.Load(DataPath("maps/urban.map"));
    fx_.Clear();
    drivingVehicle_ = -1;
    enterTimer_ = 0;
    TraceLog(LOG_INFO, "STORY: dev range is no place for a campaign, "
                       "moved to the Urban Complex");
  }
  vehicles_.Reset(world_);
  if (!server_.Start(0, &world_, world_.name(), 0)) {
    errorText_ = "Could not start the local server";
    mode_ = Mode::Error;
    return;
  }
  server_.SetBotRespawn(false);
  if (!client_.Connect("127.0.0.1", server_.port(), opts_.name)) {
    errorText_ = client_.error();
    mode_ = Mode::Error;
    return;
  }
  story_.BeginAt(opts_.storyWave > 0 ? opts_.storyWave : 1);
  devOverlay_ = false;
  devGodMode_ = false;
  devNoclip_ = false;
  mode_ = Mode::Connecting;
}

int Game::EnemiesAlive() const {
  return server_.aliveBots() + vehicles_.hostileCount();
}

// The wave's armour, put down once when the wave opens. Vans are ordinary
// SWAT vans parked as cover for the infantry; tanks and gunships hunt you.
void Game::SpawnWaveArmour() {
  const StoryWave& w = story_.spec();
  const Vector3 me = lp_.pos;
  // Ring the player at a distance, spread round the compass, so armour does
  // not all arrive from one side.
  auto ringPoint = [&](float dist, float deg) {
    const float r = DegToRadF(deg);
    Vector3 p{me.x + cosf(r) * dist, 0.0f, me.z - sinf(r) * dist};
    p.x = Clampf(p.x, 120.0f, world_.sizeX() - 120.0f);
    p.z = Clampf(p.z, 120.0f, world_.sizeZ() - 120.0f);
    return p;
  };
  const float spin = RandRange(0.0f, 360.0f);
  int placed = 0;
  for (int i = 0; i < w.tanks; ++i) {
    const Vector3 p = ringPoint(RandRange(900.0f, 1400.0f),
                                spin + i * 360.0f / fmaxf(1.0f, (float)w.tanks));
    if (vehicles_.SpawnHostile(world_, VEH_TANK, p, Color{74, 80, 62, 255}) >= 0)
      ++placed;
  }
  for (int i = 0; i < w.gunships; ++i) {
    const Vector3 p = ringPoint(RandRange(1100.0f, 1600.0f),
                                spin + 140.0f + i * 130.0f);
    if (vehicles_.SpawnHostile(world_, VEH_HELI, p, Color{58, 62, 68, 255}) >= 0)
      ++placed;
  }
  for (int i = 0; i < w.vans; ++i) {
    const Vector3 p = ringPoint(RandRange(700.0f, 1100.0f),
                                spin + 60.0f + i * 95.0f);
    if (vehicles_.SpawnHostile(world_, VEH_VAN, p, Color{28, 28, 32, 255}) >= 0)
      ++placed;
  }
  if (placed > 0)
    TraceLog(LOG_INFO, "STORY: wave %d armour -- %d tanks, %d gunships, %d vans",
             w.index, w.tanks, w.gunships, w.vans);
}

// A hostile vehicle asked to shoot this tick. VehicleSystem owns neither
// effects nor health, so this is where its trigger pull becomes something the
// player can see and feel.
void Game::ResolveHostileFire() {
  for (Vehicle& v : vehicles_.list()) {
    if (v.firedWeapon < 0) continue;
    const int weapon = v.firedWeapon;
    v.firedWeapon = -1;

    Vector3 dir = Vector3Subtract(v.fireAt, v.fireFrom);
    const float len = Vector3Length(dir);
    if (len < 1.0f) continue;
    dir = Vector3Scale(dir, 1.0f / len);

    if (weapon == 0) {
      // Tank main gun: a shell that goes off where it lands. Traced from
      // clear of the firer's own hull for the same reason the player's is --
      // otherwise it shells itself on the shot.
      int self = -1;
      for (size_t k = 0; k < vehicles_.list().size(); ++k)
        if (&vehicles_.list()[k] == &v) { self = static_cast<int>(k); break; }
      const Vector3 from = vehicles_.ClearOfHull(self, v.fireFrom, dir);
      const RayHit h = world_.Raycast(from, dir, kTankCannonRange);
      const Vector3 at = h.hit ? h.point
                               : Vector3Add(from, Vector3Scale(dir, len));
      fx_.SpawnExplosion(at, 150.0f, true);
      AddFlashLight(at, Vector3{1.0f, 0.72f, 0.32f}, 460.0f, 0.30f);
      assets_.PlayAt("explosion", at, lp_.eyePos(), lp_.yaw, 3200.0f, 1.0f);
      assets_.PlayAt("tank_cannon", v.fireFrom, lp_.eyePos(), lp_.yaw, 3600.0f,
                     0.9f);
      AddFlashLight(v.fireFrom, Vector3{1.0f, 0.85f, 0.5f}, 300.0f, 0.07f);
      ApplyLocalBlast(at, 150.0f, 90.0f);
      TrackBullet(from, dir, h.hit ? h.dist : len, h.hit, at,
                  h.hit ? h.normal : Vector3{0, 1, 0}, h.brushIndex,
                  WEAPON_ROCKET, false);
    } else {
      // Gunship minigun: a hitscan round with a tracer, and a bite of damage
      // if it lands on you.
      const WeaponDef& d = Weapon(WEAPON_RIFLE);
      const float spread = 2.4f;
      const Vector3 j = Vector3Normalize(Vector3Add(
          dir, Vector3{RandRange(-spread, spread) * 0.017f,
                       RandRange(-spread, spread) * 0.017f,
                       RandRange(-spread, spread) * 0.017f}));
      const RayHit h = world_.Raycast(v.fireFrom, j, d.range);
      float stop = h.hit ? h.dist : d.range;

      // Against the player: closest approach to the eye, same test the bots'
      // rounds get.
      const Vector3 eye = lp_.eyePos();
      const Vector3 rel = Vector3Subtract(eye, v.fireFrom);
      const float along = Clampf(Vector3DotProduct(rel, j), 0.0f, stop);
      const Vector3 near = Vector3Add(v.fireFrom, Vector3Scale(j, along));
      if (Vector3Distance(near, eye) < kPlayerRadius && !devGodMode_ &&
          !lp_.dead) {
        stop = along;
        lastHitWeapon_ = WEAPON_RIFLE;
        if (drivingVehicle_ >= 0) AbsorbVehicleDamage(11.0f, WEAPON_RIFLE);
        else lp_.Damage(11.0f);
        damageFlash_ = fminf(1.0f, damageFlash_ + 0.35f);
      }
      TrackBullet(v.fireFrom, j, stop, h.hit && stop >= (h.hit ? h.dist : 0.0f),
                  h.point, h.hit ? h.normal : Vector3{0, 1, 0}, h.brushIndex,
                  WEAPON_RIFLE, false);
      assets_.PlayAt("rifle", v.fireFrom, eye, lp_.yaw, 2600.0f, 0.5f);
      AddFlashLight(v.fireFrom, Vector3{1.0f, 0.86f, 0.5f}, 120.0f, 0.04f);
    }
  }
}

// A SWAT van that has stopped puts its squad into the street. They are
// ordinary bot slots, so they shoot, die and gib exactly like the rest of a
// wave -- they just arrive by road rather than walking in from a spawn.
void Game::DeploySwatSquads() {
  const StoryWave& w = story_.spec();
  for (Vehicle& v : vehicles_.list()) {
    if (v.dropOff <= 0) continue;
    const int n = v.dropOff;
    v.dropOff = 0;
    const VehicleDef& d = VehicleInfo(v.kind);
    // Out of the back doors, fanning both sides of the van.
    const float rad = DegToRadF(v.dirDeg);
    const Vector3 back{-cosf(rad), 0.0f, sinf(rad)};
    const Vector3 side = FlatRight(v.dirDeg);
    int placed = 0;
    for (int i = 0; i < n; ++i) {
      const float along = (d.length + d.frontLength) * 0.5f + 14.0f +
                          (i / 2) * 22.0f;
      const float across = ((i % 2) ? 1.0f : -1.0f) * (16.0f + (i / 2) * 5.0f);
      Vector3 at = Vector3Add(v.pos, Vector3Add(Vector3Scale(back, along),
                                                Vector3Scale(side, across)));
      at = world_.FindClearPoint(Vector3{at.x, v.pos.y, at.z});
      if (server_.SpawnBotAt(at, v.dirDeg + 180.0f, w.skill, w.health))
        ++placed;
    }
    if (placed > 0) {
      assets_.PlayAt("car_door", v.pos, lp_.eyePos(), lp_.yaw, 1800.0f, 0.8f);
      SetMessage("SWAT deploying", 2.0f);
      TraceLog(LOG_INFO, "STORY: SWAT van dropped %d operators", placed);
    }
    // An empty van is no longer a threat: it stops holding the wave open and
    // becomes an ordinary parked vehicle -- which you are welcome to steal.
    v.hostile = false;
    v.ai = false;
  }
}

void Game::TickStory() {
  if (!storyMode_) return;

  // Hostiles aim at the eye, which is what a wall between you and them has to
  // block for them to hold fire.
  vehicles_.SetThreat(lp_.eyePos(), !lp_.dead);

  const bool wasFighting = story_.phase() == Story::Phase::Fighting;
  story_.Tick(EnemiesAlive());

  if (story_.phase() == Story::Phase::Failed) {
    // Nothing else left to run: clear the map so the defeat screen is quiet.
    if (server_.aliveBots() > 0) server_.SetBotPopulation(0, 0.0f, kMaxHealth);
    vehicles_.SetThreat(Vector3{}, false);
    return;
  }

  if (story_.ShouldStartWave()) {
    story_.ClearStartWave();
    vehicles_.ClearHostiles();
    SpawnWaveArmour();
    const StoryWave& w = story_.spec();
    SetMessage("", 0.0f);
    TraceLog(LOG_INFO, "STORY: wave %d -- %d infantry (%d at once), skill %.2f",
             w.index, w.infantry, w.concurrent, w.skill);
  }

  if (story_.phase() == Story::Phase::Fighting) {
    // Top the map back up to the wave's concurrent cap out of whatever is
    // still queued to arrive.
    const StoryWave& w = story_.spec();
    const int alive = server_.aliveBots();
    const int room = w.concurrent - alive;
    if (room > 0 && story_.reinforcementsLeft() > 0) {
      const int want = alive + std::min(room, story_.reinforcementsLeft());
      const int added = server_.SetBotPopulation(want, w.skill, w.health);
      story_.TakeReinforcements(added);
    } else {
      // Still call in, so dead slots get recycled even when nothing is queued.
      server_.SetBotPopulation(alive, w.skill, w.health);
    }
  } else if (wasFighting && story_.phase() == Story::Phase::Cleared) {
    vehicles_.ClearHostiles();
  }
}

void Game::LeaveGame() {
  client_.Disconnect();
  server_.Stop();
  fx_.Clear();
  killFeed_.clear();
  if (storyMode_) {
    story_.Reset();
    storyMode_ = false;
    vehicles_.ClearHostiles();
    vehicles_.SetThreat(Vector3{}, false);
    server_.SetBotRespawn(true);
  }
  if (mouseCaptured_) { EnableCursor(); mouseCaptured_ = false; }
}

// --------------------------------------------------------------------- frame

void Game::Frame() {
  now_ = GetTime();
  const float dt = GetFrameTime();
  ++debugFrames_;
  if (debugTrace_ && now_ - debugLastBeat_ > 1.0) {
    debugLastBeat_ = now_;
    TraceLog(LOG_INFO,
             "BEAT t=%.1f frames=%lld phase=%d mode=%d parts=%d ents=%d dt=%.4f",
             now_, debugFrames_, debugPhase_, (int)mode_, fx_.particleCount(),
             (int)client_.entities().size(), dt);
  }
  debugPhase_ = 1;
  assets_.UpdateMusic();

  if (mode_ == Mode::Menu || mode_ == Mode::Error) {
    if (mouseCaptured_) { EnableCursor(); mouseCaptured_ = false; }
    client_.Pump(now_);
    BeginDrawing();
    DrawMenu();
    EndDrawing();
    return;
  }

  // ---- input is sampled once per frame, so nothing is ever dropped or
  //      double-counted by the fixed-step loop below.
  if (mode_ == Mode::Playing) {
    if (IsKeyPressed(KEY_ESCAPE)) {
      if (mouseCaptured_) { EnableCursor(); mouseCaptured_ = false; }
      else { mode_ = Mode::Menu; }
    }
    if (!mouseCaptured_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      DisableCursor();
      mouseCaptured_ = true;
    } else {
      HandleDevKeys();
      HandleMouseLook(dt);
      GatherInput(&frameInput_);
      if (frameInput_.firePressed) firePressedLatch_ = true;

      // In a tank the number keys pick the turret's armament, not the
      // player's, and R reloads the machine gun rather than a carried weapon.
      if (InTank() || InHeli()) {
        if (frameInput_.weaponDirect == 0) tankWeapon_ = 0;
        else if (frameInput_.weaponDirect == 1) tankWeapon_ = 1;
        if (frameInput_.reload && tankReload_ == 0) {
          bool doReload = false;
          if (InTank() && tankWeapon_ == 1 && tankMgMag_ < kTankMgMagSize) {
            tankReload_ = kTankMgReload; doReload = true;
          } else if (InHeli() && tankWeapon_ == 0 && heliBelt_ < kHeliBeltSize) {
            tankReload_ = kHeliMgReload; doReload = true;
          } else if (InHeli() && tankWeapon_ == 1 && heliRockets_ < kHeliRockets) {
            tankReload_ = kHeliRocketReload; doReload = true;
          }
          if (doReload) assets_.Play("reload", 0.6f, 0.5f, 0.8f);
        }
      } else {
        // Weapon select and reload act on this frame, not on the next tick.
        ApplyWeaponInput(frameInput_);
      }

      // Fire immediately, on the frame the click happened -- waiting for the
      // next 60 Hz tick boundary added up to 16 ms of input lag.
      if (!lp_.dead && mouseCaptured_ && debugFireWeapon_ < 0) {
        if (InTank()) TryFireTank(firePressedLatch_, frameInput_.fire);
        else if (InHeli()) TryFireHeli(firePressedLatch_, frameInput_.fire);
        else TryFire(firePressedLatch_, frameInput_.fire);
      }
      firePressedLatch_ = false;
      // Consumed; the tick only needs the held movement axes from here on.
      frameInput_.weaponDirect = -1;
      frameInput_.weaponWheel = 0;
      frameInput_.reload = false;
      frameInput_.firePressed = false;
    }
  }

  // Fixed 60 Hz simulation, rendering as fast as the display allows.
  debugPhase_ = 2;
  accumulator_ += dt;
  if (accumulator_ > 0.25) accumulator_ = 0.25;
  int steps = 0;
  while (accumulator_ >= kTickDt && steps < 8) {
    accumulator_ -= kTickDt;
    ++steps;
    debugPhase_ = 30 + steps;
    if (server_.running()) server_.Tick(now_);
    debugPhase_ = 40 + steps;
    client_.Pump(now_);
    debugPhase_ = 50 + steps;
    if (mode_ == Mode::Playing) Tick();
    ++tick_;
  }
  if (steps == 0) client_.Pump(now_);

  if (mode_ == Mode::Connecting) {
    if (client_.connected() && client_.haveSelf()) {
      mode_ = Mode::Playing;
      const PlayerState& s = client_.self();
      lp_.Reset(s.pos(), s.yaw);
      SetMessage(singlePlayer_ ? "SINGLE PLAYER  -  F1 for dev keys" : "FIGHT", 2.5f);
      assets_.Play("spawn", 0.6f);
      if (assets_.musicCount() > 0) assets_.StartMusic(GetRandomValue(0, 3));
      if (singlePlayer_) { DisableCursor(); mouseCaptured_ = true; }
    } else if (!client_.connecting() && !client_.connected()) {
      errorText_ = client_.error().empty() ? "connection failed" : client_.error();
      mode_ = Mode::Error;
    }
    BeginDrawing();
    ClearBackground(Color{12, 14, 18, 255});
    const char* t = "CONNECTING...";
    DrawText(t, GetScreenWidth() / 2 - MeasureText(t, 30) / 2,
             GetScreenHeight() / 2 - 15, 30, Color{230, 210, 140, 255});
    EndDrawing();
    return;
  }

  debugPhase_ = 4;
  ProcessEvents();

  debugPhase_ = 5;
  // Is there a roof overhead? Straight up from the eye; anything solid within
  // a few storeys counts as shelter, and the rain stops.
  {
    const RayHit up = world_.Raycast(lp_.eyePos(), Vector3{0, 1, 0}, 400.0f);
    sky_.SetShelter(up.hit ? 1.0f : 0.0f);
  }
  sky_.Update(dt, assets_, lp_.eyePos());
  debugPhase_ = 6;
  fx_.Update(dt, static_cast<float>(now_), world_, sky_.wind());
  UpdateBullets(dt);
  UpdateProjectileFlyBys();
  UpdateReloadAudio();

  // Spent cases landing. Capped per frame so a minigun burst does not fire
  // fifty clips at once, and attenuated by distance like any other world SFX.
  {
    std::vector<Vector3> hits = fx_.TakeCasingHits();
    int played = 0;
    for (const Vector3& p : hits) {
      if (++played > 3) break;
      // Short range: a case is a small bright ping, so it should be gone by
      // the time you have walked twenty metres from it. PlayAt falls off with
      // the square of distance on top of that.
      assets_.PlayAt("casing", p, lp_.eyePos(), lp_.yaw, 240.0f, 0.36f,
                     RandRange(0.9f, 1.14f));
    }
  }

  for (FlashLight& f : flashLights_) f.age += dt;
  flashLights_.erase(std::remove_if(flashLights_.begin(), flashLights_.end(),
                                    [](const FlashLight& f) {
                                      return f.age >= f.life;
                                    }),
                     flashLights_.end());

  // Rocket exhaust: burning core plus a smoke column along the flight path.
  for (const SimEntity& se : client_.entities()) {
    if (se.kind != ENT_ROCKET) continue;
    Vector3 back = Vector3Scale(se.vel, -1.0f);
    if (Vector3LengthSqr(back) < 0.01f) back = Vector3{0, -1, 0};
    fx_.RocketTrail(se.pos, Vector3Normalize(back), dt);
  }

  if (hitMarker_ > 0.0f && (hitMarker_ -= dt) <= 0.0f) hitWasHead_ = false;
  if (damageFlash_ > 0.0f) damageFlash_ -= dt * 3.4f;
  if (messageTime_ > 0.0f) messageTime_ -= dt;
  if (shake_ > 0.0f) shake_ = fmaxf(0.0f, shake_ - dt * 12.0f);
  // Viewmodel recoil settles over ~0.16 s regardless of fire rate.
  if (vmRecoilT_ > 0.0f) vmRecoilT_ = fmaxf(0.0f, vmRecoilT_ - dt * 6.2f);
  renderer_.DecayShotRecoil(dt);
  if (vmAnimDur_ > 0.0f) {
    vmAnimT_ += dt;
    vmFrame_ = static_cast<int>((vmAnimT_ / vmAnimDur_) * vmAnimFrames_);
    if (vmAnimT_ >= vmAnimDur_) { vmAnimDur_ = 0.0f; vmFrame_ = 0; }
  }

  debugPhase_ = 7;
  BeginDrawing();
  DrawGame();
  debugPhase_ = 8;
  EndDrawing();
  debugPhase_ = 9;
}

// ----------------------------------------------------------------- simulation

void Game::HandleMouseLook(float dt) {
  if (!mouseCaptured_) return;
  const Vector2 d = GetMouseDelta();
  const float k = sensitivity_ / 100.0f;
  float scale = 1.0f;
  if (lp_.zoomT > 0.05f) {
    const WeaponDef& w = Weapon(lp_.arsenal.current);
    scale = 1.0f - lp_.zoomT * (1.0f - w.zoomFov / kFovY) * 0.85f;
  }
  lp_.yaw -= d.x * k * scale;
  lp_.pitch -= d.y * k * scale;
  // fmodf, not a subtract loop: a non-finite yaw would spin such a loop
  // forever, and the mouse delta is not something this code controls.
  if (!isfinite(lp_.yaw)) lp_.yaw = 0.0f;
  lp_.yaw = fmodf(lp_.yaw, 360.0f);
  if (lp_.yaw < 0.0f) lp_.yaw += 360.0f;
  if (!isfinite(lp_.pitch)) lp_.pitch = 0.0f;
  lp_.pitch = Clampf(lp_.pitch, -kPitchLimit, kPitchLimit);
  (void)dt;
}

void Game::HandleDevKeys() {
  if (IsKeyPressed(KEY_F1)) devHelp_ = !devHelp_;
  if (IsKeyPressed(KEY_F3)) devOverlay_ = !devOverlay_;
  if (IsKeyPressed(KEY_F5)) devNoclip_ = !devNoclip_;
  if (IsKeyPressed(KEY_F6)) {
    devGodMode_ = !devGodMode_;
    SetMessage(devGodMode_ ? "GOD MODE ON" : "GOD MODE OFF", 1.2f);
  }
  if (IsKeyPressed(KEY_F7)) {
    // Cycle the weather presets.
    const WeatherPreset order[] = {WeatherPreset::Clear, WeatherPreset::Fair,
                                   WeatherPreset::Overcast, WeatherPreset::Storm,
                                   WeatherPreset::Night, WeatherPreset::Cycle};
    int i = 0;
    for (int k = 0; k < 6; ++k) if (order[k] == sky_.preset()) i = k;
    sky_.SetPreset(order[(i + 1) % 6]);
    SetMessage(std::string("WEATHER: ") + sky_.presetName(), 1.5f);
  }
  if (IsKeyDown(KEY_F8)) sky_.NudgeTimeOfDay(GetFrameTime() * 0.08f);
  if (IsKeyPressed(KEY_F9)) {
    // Test blast right in front of you.
    const Vector3 p = Vector3Add(lp_.eyePos(),
                                 Vector3Scale(FlatForward(lp_.yaw), 140.0f));
    const float g = world_.GroundHeight(p.x, p.z, 4.0f, p.y + 200.0f);
    const Vector3 at{p.x, g + 6.0f, p.z};
    fx_.SpawnExplosion(at, 120.0f, true);
    AddFlashLight(at, Vector3{1.61f, 1.01f, 0.40f}, 540.0f, 0.45f);
    assets_.PlayAt("bigboom", at, lp_.eyePos(), lp_.yaw, 2600.0f, 1.0f);
    shake_ = 4.0f;
  }
  if (IsKeyPressed(KEY_F10)) {
    lp_.arsenal.ResetFull();
    SetMessage("AMMO REFILLED", 1.2f);
  }
}

void Game::GatherInput(InputCommand* cmd) {
  *cmd = InputCommand{};
  if (!mouseCaptured_) return;

  if (IsKeyDown(KEY_W)) cmd->moveForward += 1.0f;
  if (IsKeyDown(KEY_S)) cmd->moveForward -= 1.0f;
  if (IsKeyDown(KEY_D)) cmd->moveStrafe += 1.0f;
  if (IsKeyDown(KEY_A)) cmd->moveStrafe -= 1.0f;
  cmd->jump = IsKeyDown(KEY_SPACE);
  cmd->crouch = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C);
  cmd->sneak = IsKeyDown(KEY_LEFT_SHIFT);
  cmd->fire = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  cmd->firePressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  cmd->reload = IsKeyPressed(KEY_R);
  cmd->zoom = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
  cmd->usePressed = IsKeyPressed(KEY_E);

  const float wheel = GetMouseWheelMove();
  if (wheel > 0.1f) cmd->weaponWheel = 1;
  else if (wheel < -0.1f) cmd->weaponWheel = -1;

  static const int kKeys[] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE,
                              KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_ZERO,
                              KEY_MINUS};
  for (int i = 0; i < 11 && i < WEAPON_COUNT; ++i)
    if (IsKeyPressed(kKeys[i])) cmd->weaponDirect = i;
}

void Game::Tick() {
  showScores_ = IsKeyDown(KEY_TAB) || debugScores_;
  if (debugHasTeleport_ && (debugPin_ || now_ < 3.0)) {
    lp_.pos = debugTeleport_;
    lp_.vy = 0.0f;
  }
  if (IsKeyPressed(KEY_M) && assets_.musicCount() > 0) {
    if (assets_.musicPlaying()) assets_.StopMusic();
    else assets_.StartMusic(GetRandomValue(0, assets_.musicCount() - 1));
  }

  if (debugYaw_ < 1e8f) lp_.yaw = debugYaw_;
  if (debugPitch_ < 1e8f) lp_.pitch = debugPitch_;

  InputCommand cmd = frameInput_;
  if (debugWalk_) cmd.moveForward = 1.0f;
  if (debugDrive_) {
    // Walk at the nearest vehicle, get in as soon as one is in reach, then
    // hold the throttle down. Enough to exercise entry, handling and crashes
    // in an automated run.
    cmd.moveForward = 1.0f;
    if (drivingVehicle_ < 0 && nearVehicle_ >= 0) cmd.usePressed = true;
    if (drivingVehicle_ >= 0) cmd.moveStrafe = 1.0f;   // hold a right-hand lock
  }

  if (client_.haveSelf()) {
    const PlayerState& s = client_.self();
    // Behind armour, whatever the server took off you goes into the vehicle
    // instead. The server has no idea vehicles exist, so this is where the
    // hit gets redirected.
    if (drivingVehicle_ >= 0 && s.health < lastServerHealth_ - 0.01f) {
      AbsorbVehicleDamage(lastServerHealth_ - s.health, lastHitWeapon_);
    }
    lastServerHealth_ = s.health;
    if (!devGodMode_ && drivingVehicle_ < 0) {
      lp_.health = s.health;
      lp_.armor = s.armor;
      const bool serverDead = s.dead();
      if (serverDead && !lp_.dead) {
        respawnIn_ = kRespawnTime;
        // Edge-triggered: the campaign spends a life here rather than testing
        // "still dead" later, which raced the server's own respawn timer and
        // meant a death sometimes cost nothing at all.
        if (storyMode_) story_.NotifyDeath();
      }
      lp_.dead = serverDead;
    } else {
      lp_.health = kMaxHealth;
      lp_.dead = false;
    }
  }
  if (lp_.dead) respawnIn_ = fmaxf(0.0f, respawnIn_ - kTickDt);

  if (!lp_.dead && lp_.arsenal.NeedsReload() && cmd.fire)
    lp_.arsenal.BeginReload();

  // Order matters: the campaign points the hostiles at you, TickVehicles runs
  // their brains (which is when they pull a trigger), and only then can that
  // trigger pull be turned into tracers, blasts and damage.
  TickStory();
  TickVehicles(cmd);
  ResolveHostileFire();
  if (storyMode_) DeploySwatSquads();
  if (drivingVehicle_ >= 0) {
    // The chassis carries the player; nothing else in the on-foot movement
    // path should run while we are in it.
    lp_.arsenal.Tick();
    if (debugFireWeapon_ >= 0 && debugFireWeapon_ < WEAPON_COUNT && !lp_.dead) {
      if (InTank()) {
        // In an automated run the debug weapon index picks the turret's
        // armament instead: 0 the main gun, 1 the roof machine gun.
        tankWeapon_ = debugFireWeapon_ == 1 ? 1 : 0;
        TryFireTank(true, true);
      } else if (InHeli()) {
        // ...and the gunship's: 0 the minigun, 1 the rocket pods.
        tankWeapon_ = debugFireWeapon_ == 1 ? 1 : 0;
        TryFireHeli(true, true);
      } else {
        lp_.arsenal.current = static_cast<uint8_t>(debugFireWeapon_);
        lp_.arsenal.cur().reserve = 99;
        TryFire(true, true);
      }
    }
    client_.SendInput(lp_, tick_);
    return;
  }

  if (devNoclip_) {
    // Free-fly for level inspection.
    const Vector3 f = lp_.aimDir();
    const Vector3 r = FlatRight(lp_.yaw);
    const float sp = cmd.sneak ? 2.0f : 8.0f;
    lp_.pos = Vector3Add(lp_.pos, Vector3Scale(f, cmd.moveForward * sp));
    lp_.pos = Vector3Add(lp_.pos, Vector3Scale(r, cmd.moveStrafe * sp));
    if (cmd.jump) lp_.pos.y += sp;
    if (cmd.crouch) lp_.pos.y -= sp;
    lp_.vy = 0.0f;
    lp_.arsenal.Tick();
  } else {
    const bool wasGrounded = lp_.onGround;
    lp_.Tick(cmd, world_);
    // GTJ3D played snd_jump the instant you left the ground.
    if (wasGrounded && !lp_.onGround && lp_.vy > 0.0f)
      assets_.Play("jump", 0.55f, 0.5f, RandRange(0.95f, 1.06f));

    // --- fall damage ------------------------------------------------------
    // The server owns health, so this goes through the same self-inflicted
    // damage path an explosive does: it is reported as a hit on ourselves.
    if (lp_.landImpact > 0.0f) {
      const float dmg = LocalPlayer::FallDamage(lp_.landImpact);
      if (dmg > 0.0f) {
        if (!devGodMode_) {
          client_.SendFall(dmg);
          lp_.Damage(dmg);
        }
        damageFlash_ = fminf(1.0f, damageFlash_ + Clampf(dmg / 60.0f, 0.2f, 1.0f));
        shake_ = fmaxf(shake_, Clampf(dmg / 30.0f, 0.3f, 2.2f));
        assets_.Play(dmg > 45.0f ? "hurt2" : "hurt", 0.7f, 0.5f,
                     RandRange(0.94f, 1.06f));
      }
      // Deliberately nothing on a landing that did no damage. A thump on
      // every touchdown lands a beat after snd_jump and reads as the jump
      // sound playing twice.
    }
  }

  if (debugFireWeapon_ >= 0 && debugFireWeapon_ < WEAPON_COUNT && !lp_.dead) {
    lp_.arsenal.current = static_cast<uint8_t>(debugFireWeapon_);
    // Top up the reserve but never the magazine, so the weapon runs dry and
    // reloads for real -- that is what exercises the reload animation.
    lp_.arsenal.cur().reserve = 99;
    if (lp_.arsenal.NeedsReload()) lp_.arsenal.BeginReload();
    TryFire(true, true);
  }

  client_.SendInput(lp_, tick_);
}

// ------------------------------------------------------------------- shooting

// Unproject the weapon's measured muzzle point from HUD space into the world,
// so the flash and every tracer start exactly at the drawn barrel tip.
Vector3 Game::MuzzlePos() const {
  const WeaponDef& d = Weapon(lp_.arsenal.current);
  const Vector3 eye = lp_.eyePos();
  const Vector3 fwd = lp_.aimDir();
  Vector3 right = Vector3CrossProduct(fwd, Vector3{0, 1, 0});
  if (Vector3LengthSqr(right) < 1e-6f) right = FlatRight(lp_.yaw);
  right = Vector3Normalize(right);
  const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, fwd));

  const int sw = GetScreenWidth(), sh = GetScreenHeight();
  const HudTransform hud = HudTransform::For(sw, sh);
  // The viewmodel bobs with the camera, so the muzzle has to bob with it.
  const float bobX = sinf(lp_.bobPhase) * 7.0f * lp_.bobAmount;
  const float bobY = fabsf(cosf(lp_.bobPhase)) * 5.0f * lp_.bobAmount;
  const Vector2 sp = hud.P(d.muzzleHudX + bobX, d.muzzleHudY + bobY);

  const float ndcX = (sp.x / (float)sw) * 2.0f - 1.0f;
  const float ndcY = 1.0f - (sp.y / (float)sh) * 2.0f;

  const WeaponDef& cur = Weapon(lp_.arsenal.current);
  const float fovy = kFovY + (cur.zoomFov - kFovY) * (cur.canZoom ? lp_.zoomT : 0.0f);
  const float tanH = tanf(fovy * 0.5f * DEG2RAD);
  const float aspect = (sh > 0) ? (float)sw / (float)sh : 1.0f;

  // Distance out along the view axis. Scoped in, the muzzle collapses toward
  // the sight line so a scoped shot does not appear to leave from off-centre.
  const float dist = 16.0f;
  const float lateral = 1.0f - lp_.zoomT * 0.9f;

  return Vector3Add(
      Vector3Add(eye, Vector3Scale(fwd, dist)),
      Vector3Add(Vector3Scale(right, ndcX * tanH * aspect * dist * lateral),
                 Vector3Scale(up, ndcY * tanH * dist * lateral)));
}

int Game::TracePlayers(Vector3 origin, Vector3 dir, float maxDist, float* outT,
                       bool* outHead, bool* outLeg) const {
  int best = -1;
  float bestT = maxDist;
  const RemotePlayer* ps = client_.players();
  const double rt = now_ - 0.08;

  for (int i = 0; i < kMaxPlayers; ++i) {
    if (i == client_.myId() || !ps[i].active || ps[i].cur.dead()) continue;
    float yaw, pitch;
    const Vector3 feet = ps[i].Interp(rt, &yaw, &pitch);
    const float h = ps[i].cur.height();
    float t, localY;
    if (!RayCylinder(origin, dir, feet, kPlayerRadius, h, &t, &localY)) continue;
    if (t >= bestT) continue;
    bestT = t;
    best = i;
    *outHead = localY > h * kHeadZone;
    *outLeg = localY < h * kLegZone;
  }
  *outT = bestT;
  return best;
}

// Blood thrown off a wound, landing on whatever happens to be behind it.
//
// Each spot is its own short raycast in its own direction rather than one
// decal stamped under the body, which is what lets the splatter climb walls,
// run across a ceiling and wrap a corner: the cone is centred on the shot
// direction -- material leaves a wound the way the round was going -- with a
// wide spread, a downward bias for the part that just falls, and one cast
// straight down for the pool underneath.
void Game::SplatterBlood(Vector3 wound, Vector3 shotDir, float amount) {
  if (Vector3LengthSqr(shotDir) < 0.001f) shotDir = Vector3{0, -1, 0};
  shotDir = Vector3Normalize(shotDir);
  const int casts = Clampf(amount, 1.0f, 18.0f);
  // How far blood carries. Past this it has thinned out to nothing, and a
  // decal appearing on a wall thirty metres away reads as a bug.
  constexpr float kReach = 260.0f;

  for (int i = 0; i < casts; ++i) {
    // Mostly along the exit path, with a wide cone and a sag toward the
    // floor. Every spot is rolled fresh, so no two hits stamp the same
    // pattern twice.
    Vector3 d = shotDir;
    if (i == 0) {
      d = Vector3{0.0f, -1.0f, 0.0f};        // the pool under the wound
    } else {
      const float spread = 0.75f;
      d = Vector3Normalize(Vector3Add(
          shotDir, Vector3{RandRange(-spread, spread),
                           RandRange(-spread, spread) - 0.35f,
                           RandRange(-spread, spread)}));
    }
    const RayHit h = world_.Raycast(Vector3Add(wound, Vector3Scale(d, 1.5f)), d,
                                    kReach);
    if (!h.hit) continue;
    // Thins out with distance: a wall right behind someone takes a heavy
    // spot, a far one takes a fleck.
    const float fall = 1.0f - Clampf(h.dist / kReach, 0.0f, 1.0f);
    const float size = RandRange(3.5f, 9.0f) * (0.45f + fall * 0.85f) *
                       Clampf(amount / 6.0f, 0.6f, 1.5f);
    if (size < 1.2f) continue;
    // Arterial red through to nearly dry, varied per spot so a patch of
    // splatter has depth in it rather than being one flat colour.
    const unsigned char r =
        static_cast<unsigned char>(RandRange(96.0f, 168.0f));
    const Color c{r, static_cast<unsigned char>(RandRange(6.0f, 22.0f)),
                  static_cast<unsigned char>(RandRange(8.0f, 24.0f)),
                  static_cast<unsigned char>(RandRange(190.0f, 245.0f))};
    fx_.AddDecal(h.point, h.normal, size, c, RandRange(55.0f, 95.0f));
  }
}

// A rocket or a grenade going past your head. Bullets get their whoosh from
// BulletTrace, which is a client-side prediction of a hitscan and knows the
// whole flight in advance; a projectile is a server-simulated entity that
// only ever reports where it is now, so the closest approach is found by
// watching the distance turn around -- the frame it stops falling is the
// frame the thing is level with you.
void Game::UpdateProjectileFlyBys() {
  const Vector3 ear = lp_.eyePos();
  const std::vector<SimEntity>& ents = client_.entities();

  // Drop tracks whose entity has gone (detonated, or out of the snapshot).
  for (size_t i = flyBys_.size(); i-- > 0;) {
    bool alive = false;
    for (const SimEntity& e : ents)
      if (e.id == flyBys_[i].id) { alive = true; break; }
    if (!alive) flyBys_.erase(flyBys_.begin() + i);
  }

  for (const SimEntity& e : ents) {
    // Only the things that actually fly. A mine on the floor is not a
    // near miss.
    if (e.kind != ENT_ROCKET && e.kind != ENT_GRENADE) continue;
    // Our own ordnance never whooshes at us, the same rule bullets follow.
    if (e.owner == client_.myId()) continue;

    FlyByTrack* t = nullptr;
    for (FlyByTrack& f : flyBys_)
      if (f.id == e.id) { t = &f; break; }
    if (!t) {
      flyBys_.push_back(FlyByTrack{e.id, 1e9f, false});
      t = &flyBys_.back();
    }

    const float d = Vector3Distance(e.pos, ear);
    // Two ways to earn a whoosh, because closest approach on its own is not
    // enough: these things detonate on contact, so one aimed anywhere near
    // you hits you or the wall behind you and the distance never turns
    // around. So it also fires the moment something inbound gets close --
    // which is when you would hear it arriving anyway.
    constexpr float kNear = 34.0f;    // inside this, the tighter near-miss
    constexpr float kPass = 150.0f;   // audible at all
    constexpr float kArrive = 90.0f;  // close enough to hear it coming
    const bool receding = d > t->lastDist;
    if (!t->played && (d < kArrive || (receding && t->lastDist < kPass))) {
      t->played = true;
      const float miss = fminf(d, t->lastDist);
      // Tighter inside a few metres, the wider passby further out -- the same
      // split the bullet audio uses.
      const char* clip = miss < kNear ? "nearmiss" : "passby";
      const float vol = Clampf(1.0f - miss / kPass, 0.12f, 1.0f) *
                        (e.kind == ENT_ROCKET ? 0.95f : 0.6f);
      assets_.PlayAt(clip, e.pos, ear, lp_.yaw, kPass * 2.0f,
                     vol, RandRange(0.7f, 0.85f));
    }
    t->lastDist = d;
  }
}

void Game::SurfaceImpact(Vector3 point, Vector3 normal, int brushIndex,
                         bool loud) {
  fx_.ImpactPuff(point, normal, 3.2f, DustFor(world_, brushIndex));
  // No holes on a vehicle. Its collider is an axis-aligned box that does not
  // follow the bodywork, so a decal stuck to it would float beside the car --
  // and it would be left behind in mid-air the moment the car drove off.
  const bool valid = brushIndex >= 0 &&
                     brushIndex < (int)world_.brushes().size();
  const bool onVehicle = valid && world_.brushes()[brushIndex].invisible;
  if (!onVehicle) {
    // Hand the struck brush's bounds over so the mark can be trimmed to the
    // face rather than hanging out over a corner.
    const Vector3 mn = valid ? world_.brushes()[brushIndex].min : Vector3{};
    const Vector3 mx = valid ? world_.brushes()[brushIndex].max : Vector3{};
    fx_.AddBulletHole(point, normal, mn, mx);
  }
  if (!loud) return;
  // Every strike on the world gets its impact layer; metal and glancing hits
  // add a ricochet on top.
  assets_.PlayAt("nearhit", point, lp_.eyePos(), lp_.yaw, 1100.0f, 0.6f,
                 RandRange(0.92f, 1.1f));
  const bool metal = IsMetal(world_, brushIndex);
  if (metal ? GetRandomValue(0, 1) == 0 : GetRandomValue(0, 3) == 0) {
    assets_.PlayAt("ricochet", point, lp_.eyePos(), lp_.yaw, 1300.0f, 0.55f,
                   RandRange(0.9f, 1.15f));
  }
}

// Muzzle velocity per weapon, in world units per second (11 units ~ 1 m).
static float BulletSpeedFor(int weapon) {
  switch (weapon) {
    case WEAPON_SNIPER:  return 9500.0f;   // ~860 m/s
    case WEAPON_RIFLE:   return 8800.0f;
    case WEAPON_SMG:     return 4600.0f;
    case WEAPON_PISTOL:  return 4000.0f;
    case WEAPON_SHOTGUN: return 4200.0f;
    default:             return 7000.0f;
  }
}

void Game::TrackBullet(Vector3 from, Vector3 dir, float stopDist, bool hitWorld,
                       Vector3 hitPoint, Vector3 hitNormal, int brushIndex,
                       uint8_t weapon, bool selfShot) {
  if (bullets_.size() > 160) bullets_.erase(bullets_.begin());
  BulletTrace b;
  b.origin = from;
  b.dir = dir;
  b.speed = BulletSpeedFor(weapon);
  b.hitDist = stopDist;
  b.hitWorld = hitWorld;
  b.hitPoint = hitPoint;
  b.hitNormal = hitNormal;
  b.brushIndex = brushIndex;
  b.weapon = weapon;
  b.selfShot = selfShot;

  // Closest approach of the flight path to the listener, as a distance along
  // the path. If that lies beyond where the round stops, it never gets there.
  const Vector3 head = lp_.eyePos();
  const float along = Vector3DotProduct(Vector3Subtract(head, from), dir);
  b.missAt = Clampf(along, 0.0f, stopDist);
  b.missDist = Vector3Distance(head, Vector3Add(from, Vector3Scale(dir, b.missAt)));
  bullets_.push_back(b);
}

// Weapon switching and reloading, applied the instant the key goes down.
void Game::ApplyWeaponInput(const InputCommand& cmd) {
  if (lp_.dead) return;
  Arsenal& a = lp_.arsenal;

  if (cmd.weaponWheel != 0) {
    const uint8_t next = a.NextUsable(cmd.weaponWheel);
    if (next != a.current) {
      a.FinishReload();
      a.current = next;
      vmAnimDur_ = 0.0f;
      vmFrame_ = 0;
      vmRecoilT_ = 0.0f;
    }
  }
  if (cmd.weaponDirect >= 0 && cmd.weaponDirect < WEAPON_COUNT &&
      cmd.weaponDirect != a.current && a.HasAmmo(cmd.weaponDirect)) {
    a.FinishReload();
    a.current = static_cast<uint8_t>(cmd.weaponDirect);
    // No draw delay at all -- the weapon is up and ready on this frame.
    a.cur().cooldown = 0;
    vmAnimDur_ = 0.0f;
    vmFrame_ = 0;
    vmRecoilT_ = 0.0f;
  }
  if (cmd.reload) {
    a.cur().cooldown = 0;
    a.BeginReload();
  }
}

void Game::AddFlashLight(Vector3 pos, Vector3 color, float radius, float life) {
  if (flashLights_.size() > 24) flashLights_.erase(flashLights_.begin());
  FlashLight f;
  f.pos = pos;
  f.color = color;
  f.radius = radius;
  f.life = life;
  flashLights_.push_back(f);
}

void Game::UpdateBullets(float dt) {
  const Vector3 head = lp_.eyePos();
  for (BulletTrace& b : bullets_) {
    const float prev = b.travelled;
    b.travelled += b.speed * dt;

    // Fly-by: only if the round genuinely passes the ear before it stops.
    if (!b.playedWhoosh && !b.selfShot && b.missDist < 95.0f &&
        b.missAt < b.hitDist && b.travelled >= b.missAt && prev < b.hitDist) {
      b.playedWhoosh = true;
      const Vector3 at = Vector3Add(b.origin, Vector3Scale(b.dir, b.missAt));
      const float vol =
          (b.missDist < 40.0f)
              ? 0.9f
              : 0.9f * Clampf(1.0f - (b.missDist - 40.0f) / 55.0f, 0.12f, 1.0f);
      assets_.PlayAt(b.missDist < 26.0f ? "nearmiss" : "passby", at, head,
                     lp_.yaw, 300.0f, vol, RandRange(0.94f, 1.08f));
    }

    // Impact: the round has arrived.
    if (b.travelled >= b.hitDist) {
      if (b.hitWorld) SurfaceImpact(b.hitPoint, b.hitNormal, b.brushIndex, true);
      b.travelled = -1.0f;   // marked for removal
    }
  }
  bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                [](const BulletTrace& b) {
                                  return b.travelled < 0.0f;
                                }),
                 bullets_.end());
}

// Per-weapon reload audio, assembled from GTJ3D's own clips: the shotgun uses
// snd_reload_shotgun once per shell, magazine weapons get a mag-out, mag-in
// and charging-handle sequence at fixed points through the reload.
void Game::UpdateReloadAudio() {
  const int w = lp_.arsenal.current;
  const WeaponSlot& s = lp_.arsenal.cur();
  const WeaponDef& d = Weapon(w);

  if (!s.reloading || d.reloadTicks <= 0) {
    reloadAudioWeapon_ = -1;
    reloadShellCount_ = -1;
    return;
  }

  const float t = 1.0f - Clampf((float)s.reloadTimer / (float)d.reloadTicks,
                                0.0f, 1.0f);
  if (reloadAudioWeapon_ != w) {
    reloadAudioWeapon_ = w;
    reloadAudioPrev_ = -1.0f;
    reloadShellCount_ = -1;
  }

  // GTJ3D only ever shipped one reload clip -- snd_reload_shotgun -- so that is
  // what every weapon uses, pitched to suit. The shotgun plays it as each shell
  // seats; magazine weapons play it on the mag going in and again on the
  // charging handle, which is the same action twice at different pitches.
  // One sound per reload. GTJ3D's snd_reload_shotgun is the only reload clip
  // it shipped, so that is the sound: once per shell for the shotgun, once per
  // magazine for everything else.
  if (d.shellReload) {
    if (reloadShellCount_ != s.mag) {
      reloadShellCount_ = s.mag;
      assets_.Play("reload", 0.6f, 0.5f, RandRange(0.95f, 1.06f));
    }
  } else if (reloadAudioPrev_ < 0.0f) {
    // The super shotgun breaks open rather than seating a magazine, so it gets
    // its own heavier clack (see tools/make_ssg_sound.py).
    if (w == WEAPON_SUPERSHOTGUN) assets_.Play("ssg_break", 0.7f, 0.5f, 1.0f);
    else assets_.Play("reload", 0.6f, 0.5f, w == WEAPON_ROCKET ? 0.75f : 1.0f);
  }
  reloadAudioPrev_ = t;
}

// GTJ3D's obj_player car block: E to get in when you are close enough and
// facing it, a one-second door-and-start wait, then the chassis carries you.
// Space gets you out with a small upward kick, exactly as it did there.
void Game::TickVehicles(const InputCommand& in) {
  DriveInput di;

  // Tank armament timers run on the 60 Hz tick, like every other cooldown.
  if (tankCooldown_ > 0) --tankCooldown_;
  if (tankReload_ > 0 && --tankReload_ == 0) {
    tankMgMag_ = kTankMgMagSize;
    heliBelt_ = kHeliBeltSize;
    heliRockets_ = kHeliRockets;
  }

  // The machine gun's burst loop follows the rounds: alive while they are
  // going out, cut within a couple of ticks of the trigger coming up. It
  // covers the gunship's minigun as well as the tank's roof gun -- the
  // gunship had no firing sound at all, because this test only ever looked
  // at the tank.
  const bool mgOn = tankReload_ == 0 && tick_ - mgFireTick_ < 5 &&
                    ((InTank() && tankWeapon_ == 1) ||
                     (InHeli() && tankWeapon_ == 0));
  if (mgOn && !mgLoop_) { assets_.PlayLoop("tank_mg", 0.55f); mgLoop_ = true; }
  else if (mgOn) assets_.SetLoopVolume("tank_mg", 0.55f);
  else if (mgLoop_) { assets_.StopLoop("tank_mg"); mgLoop_ = false; }

  // --- getting out --------------------------------------------------------
  if (drivingVehicle_ >= 0 && enterTimer_ == 0) {
    const bool bail = in.jump || in.usePressed || lp_.dead ||
                      vehicles_.list()[drivingVehicle_].life <= 0.0f;
    if (bail) {
      Vehicle& v = vehicles_.list()[drivingVehicle_];
      v.driver = -1;
      v.engineOn = false;
      const VehicleDef& d = VehicleInfo(v.kind);
      lp_.pos = FindExitSpot(v);
      lp_.vy = 2.0f;
      lp_.speed = 0.0f;
      lp_.onGround = false;
      drivingVehicle_ = -1;
      driveTurning_ = 0;
      if (engineAudio_) {
        assets_.StopLoop("engine");
        assets_.StopLoop("heli");
        engineAudio_ = false;
      }
      driveThrottlePrev_ = false;
      assets_.Play("car_door", 0.6f);
      SetMessage("", 0.0f);
      vehicles_.Tick(world_, -1, di);
      return;
    }
  }

  // --- getting in ---------------------------------------------------------
  if (drivingVehicle_ < 0 && !lp_.dead) {
    nearVehicle_ = vehicles_.FindEnterable(lp_.pos, lp_.yaw);
    if (nearVehicle_ >= 0 && in.usePressed) {
      drivingVehicle_ = nearVehicle_;
      vehicles_.list()[drivingVehicle_].driver = client_.myId();
      enterTimer_ = 60;                       // GTJ's entering_car_timer
      assets_.Play("car_door", 0.7f);
      lp_.yaw = vehicles_.list()[drivingVehicle_].dirDeg;
      lp_.speed = 0.0f;
      lp_.vy = 0.0f;
      // LocalPlayer::Tick is what decays these, and it does not run while you
      // are in a vehicle -- so whatever the run bob and the crouch happened to
      // be on the frame you got in would otherwise stay frozen into the
      // driving camera for as long as you drove.
      lp_.bobAmount = 0.0f;
      lp_.bobHeight = 0.0f;
      lp_.bobPhase = 0.0f;
      lp_.landDip = 0.0f;
      lp_.crouchT = 0.0f;
      lp_.crouch = false;
    }
  } else {
    nearVehicle_ = -1;
  }

  // --- the door-and-start second -----------------------------------------
  if (enterTimer_ > 0) {
    if (--enterTimer_ == 0) {
      assets_.Play("car_start", 0.65f);
      SetMessage("SPACE or E to get out", 2.5f);
    }
    vehicles_.Tick(world_, -1, di);
    if (drivingVehicle_ >= 0) lp_.pos = vehicles_.SeatPos(drivingVehicle_);
    return;
  }

  if (drivingVehicle_ >= 0) {
    di.fwd = in.moveForward > 0.5f;
    di.back = in.moveForward < -0.5f;
    di.left = in.moveStrafe < -0.5f;
    di.right = in.moveStrafe > 0.5f;
    // GTJ's `turning`, which is what picks the steering wheel sprite frame.
    driveTurning_ = di.left ? 1 : (di.right ? 2 : 0);
    // The gunship is flown with the mouse: the nose follows where you look,
    // and how far you are looking down is how far it tips forward.
    if (vehicles_.list()[drivingVehicle_].kind == VEH_HELI) {
      di.haveAir = true;
      di.heading = lp_.yaw;
      // Positive is nose-down, which is what pulls it forward, so looking
      // down flies you forward and looking up brakes and backs off.
      di.pitch = Clampf(-lp_.viewPitch(), -30.0f, 38.0f);
    }
  } else {
    driveTurning_ = 0;
  }

  // Engine audio, as GTJ3D ran it: snd_engine loops while the throttle is
  // down, snd_accel fires once on each press, both stop the moment you lift
  // off or get out.
  // Only while actually rolling -- a parked car you happen to be sitting in
  // should be silent.
  // A helicopter's rotor runs whenever you are in it, moving or not.
  const bool wantEngine =
      drivingVehicle_ >= 0 && enterTimer_ == 0 &&
      (vehicles_.list()[drivingVehicle_].kind == VEH_HELI ||
       fabsf(vehicles_.list()[drivingVehicle_].speed) > 0.08f);
  if (wantEngine && InHeli()) {
    const Vehicle& hv = vehicles_.list()[drivingVehicle_];
    if (!engineAudio_) { assets_.PlayLoop("heli", 0.5f); engineAudio_ = true; }
    assets_.SetLoopVolume("heli", 0.22f + hv.spool * 0.42f);
    driveThrottlePrev_ = false;
  } else if (wantEngine) {
    const Vehicle& ev = vehicles_.list()[drivingVehicle_];
    const float top = fmaxf(1.0f, VehicleInfo(ev.kind).speedMax);
    const float rev = Clampf(fabsf(ev.speed) / top, 0.0f, 1.0f);
    if (!engineAudio_) { assets_.PlayLoop("engine", 0.20f); engineAudio_ = true; }
    // Idle bed that swells with road speed; the clip cannot be pitched, so
    // volume carries the load the way it did in GTJ.
    assets_.SetLoopVolume("engine", 0.18f + rev * 0.42f);
    if (di.fwd && !driveThrottlePrev_) assets_.Play("accel", 0.55f);
    driveThrottlePrev_ = di.fwd;
  } else if (engineAudio_) {
    assets_.StopLoop("engine");
    assets_.StopLoop("heli");
    engineAudio_ = false;
    driveThrottlePrev_ = false;
  }

  const float dirBefore =
      drivingVehicle_ >= 0 ? vehicles_.list()[drivingVehicle_].dirDeg : 0.0f;
  vehicles_.Tick(world_, drivingVehicle_, di);

  if (drivingVehicle_ >= 0) {
    Vehicle& v = vehicles_.list()[drivingVehicle_];
    if (v.kind == VEH_TANK) {
      // The turret traverses on its own. A/D steer the tracks underneath it
      // and the hull turns without dragging your aim around with it, so the
      // mouse only ever moves the gun.
      v.turretDeg = lp_.yaw;
    } else if (v.kind == VEH_HELI) {
      // The gunship's heading *is* lp_.yaw -- VehicleSystem::Tick assigns it
      // straight across from di.heading. Carrying the heading change back into
      // the yaw as well would apply every mouse movement twice and leave the
      // airframe a tick behind the view, which read as the whole helicopter
      // sliding out from under a POV that would not sit still.
    } else {
      // GTJ swung the camera onto the car's heading as it turned (view_dir +=
      // turning). Mouse look still stacks on top, so you can look around
      // freely while the view tracks the road.
      lp_.yaw += v.dirDeg - dirBefore;
    }
    lp_.pos = vehicles_.SeatPos(drivingVehicle_);
    lp_.vy = 0.0f;
    lp_.onGround = true;
    lp_.speed = v.speed;

    if (v.crashImpulse > 0.0f) {
      shake_ = fminf(1.4f, shake_ + v.crashImpulse * 0.09f);
      assets_.Play("crash", 0.8f, 0.5f, RandRange(0.9f, 1.08f));
      if (!devGodMode_) lp_.Damage(v.crashImpulse * 0.6f);
    }
    if (v.life <= 0.0f) {
      fx_.SpawnExplosion(Vector3{v.pos.x, v.pos.y + 14.0f, v.pos.z}, 150.0f,
                         true);
      assets_.Play("explosion", 1.0f);
    }
  }
}

// Somewhere clear to stand when you get out. The driver's door is tried
// first, then the other side, then a widening ring around the vehicle -- a
// car parked hard against a wall, or wedged in an alley, used to drop you
// inside the geometry.
Vector3 Game::FindExitSpot(const Vehicle& v) const {
  const VehicleDef& d = VehicleInfo(v.kind);
  const float side = d.width * 0.5f + kPlayerRadius + 6.0f;
  const float nose = (d.length + d.frontLength) * 0.5f + kPlayerRadius + 6.0f;

  // Candidate bearings relative to the chassis: both doors first, then the
  // corners, then front and back.
  static const float kBearings[] = {90.0f, -90.0f, 135.0f, -135.0f,
                                    45.0f, -45.0f, 180.0f, 0.0f};
  for (float ring = 1.0f; ring <= 2.6f; ring += 0.55f) {
    for (float bearing : kBearings) {
      const float rad = DegToRadF(v.dirDeg + bearing);
      // Interpolate the reach between side and nose by how far off the beam
      // the bearing is, so a long car does not drop you inside its own bonnet.
      const float along = fabsf(cosf(DegToRadF(bearing)));
      const float reach = (side + (nose - side) * along) * ring;
      Vector3 p{v.pos.x + cosf(rad) * reach, v.pos.y + 2.0f,
                v.pos.z - sinf(rad) * reach};
      p.y = world_.GroundHeight(p.x, p.z, kPlayerRadius, v.pos.y + 60.0f);
      if (world_.IsFree(p, kPlayerRadius, kPlayerHeight)) return p;
    }
  }
  // Nothing clear anywhere around it: put the player on the roof, which is
  // always free because the vehicle's own collider ends there.
  return Vector3{v.pos.x, v.pos.y + d.height + 2.0f, v.pos.z};
}

// Blast damage against vehicles. The server owns damage to players, but it
// knows nothing about vehicles, so anything with a blast radius has to hurt
// them here.
void Game::ApplyLocalBlast(Vector3 at, float radius, float damage) {
  for (Vehicle& v : vehicles_.list()) {
    if (v.life <= 0.0f) continue;
    const Vector3 c{v.pos.x, v.pos.y + VehicleInfo(v.kind).height * 0.5f,
                    v.pos.z};
    const float dist = Vector3Distance(c, at);
    if (dist > radius) continue;
    const float fall = 1.0f - dist / radius;
    v.life -= damage * fall * fall;
    if (v.life <= 0.0f) {
      fx_.SpawnExplosion(Vector3{v.pos.x, v.pos.y + 16.0f, v.pos.z}, 190.0f,
                         true);
      assets_.PlayAt("explosion", v.pos, lp_.eyePos(), lp_.yaw, 3000.0f, 0.95f);
    }
  }
}

// True for the things a tank's armour cannot simply shrug off. Everything
// else -- pistols, rifles, shotguns, the sniper -- rings off the hull.
bool Game::HurtsTank(int weapon) {
  return weapon == WEAPON_ROCKET || weapon == WEAPON_GRENADE ||
         weapon == WEAPON_MINE || weapon == WEAPON_TRIPFLARE;
}

// Damage arriving while you are in a vehicle goes into the vehicle first --
// you are behind its armour, not standing in the open.
void Game::AbsorbVehicleDamage(float amount, int weapon) {
  if (drivingVehicle_ < 0 || amount <= 0.0f) return;
  Vehicle& v = vehicles_.list()[drivingVehicle_];

  if (v.kind == VEH_TANK && !HurtsTank(weapon)) {
    // Small arms do nothing to a tank but make a noise.
    if (now_ - lastRicochet_ > 0.06) {
      lastRicochet_ = now_;
      assets_.Play("ricochet", 0.55f, 0.5f, RandRange(0.85f, 1.2f));
      fx_.ImpactPuff(Vector3{v.pos.x, v.pos.y + 20.0f, v.pos.z},
                     Vector3{0, 1, 0}, 1.6f, Color{190, 186, 176, 255});
    }
    return;
  }

  v.life -= amount * (v.kind == VEH_TANK ? 1.0f : 1.6f);
  if (v.life <= 0.0f) {
    fx_.SpawnExplosion(Vector3{v.pos.x, v.pos.y + 16.0f, v.pos.z}, 190.0f, true);
    assets_.Play("explosion", 1.0f);
  }
}

// ------------------------------------------------------------ tank gunnery
bool Game::InTank() const {
  return drivingVehicle_ >= 0 && enterTimer_ == 0 &&
         vehicles_.list()[drivingVehicle_].kind == VEH_TANK;
}

bool Game::InHeli() const {
  return drivingVehicle_ >= 0 && enterTimer_ == 0 &&
         vehicles_.list()[drivingVehicle_].kind == VEH_HELI;
}

// The gunship's armament. Both guns live on the wings and alternate sides
// shot for shot, which is what gives a minigun burst its side-to-side wobble
// and keeps a rocket salvo balanced.
void Game::TryFireHeli(bool pressed, bool held) {
  if (!InHeli() || tankCooldown_ > 0 || tankReload_ > 0) return;
  Vehicle& v = vehicles_.list()[drivingVehicle_];
  const VehicleDef& d = VehicleInfo(v.kind);

  const Vector3 eye = lp_.eyePos();
  const Vector3 aim = lp_.aimDir();
  // Wing hardpoints, left and right of the fuselage, alternating.
  const float rad = DegToRadF(v.dirDeg);
  const Vector3 rt{-sinf(rad), 0.0f, -cosf(rad)};
  const float sideSign = heliSide_ ? 1.0f : -1.0f;
  heliSide_ = !heliSide_;
  const Vector3 hard = Vector3Add(
      Vector3{v.pos.x, v.pos.y + d.modelHeight * 0.45f, v.pos.z},
      Vector3Scale(rt, sideSign * d.width * 0.46f));

  if (tankWeapon_ == 1) {
    // --- rocket pods, ten a side -----------------------------------------
    if (!pressed) return;
    if (heliRockets_ <= 0) {
      tankReload_ = kHeliRocketReload;
      assets_.Play("beep2", 0.35f, 0.5f, 0.7f);
      assets_.Play("reload", 0.55f, 0.5f, 0.78f);
      return;
    }
    --heliRockets_;
    tankCooldown_ = kHeliRocketCooldown;
    // The pylon is inside the gunship's own collider -- a rocket launched
    // from it detonated on the frame it was created, which read as the pods
    // simply not working. Launch from where the round actually leaves the
    // airframe instead.
    const Vector3 launch = vehicles_.ClearOfHull(drivingVehicle_, hard, aim);
    // Exactly the launcher the player carries, so the warhead, blast and
    // trail all behave the way they do on foot.
    client_.SendFire(WEAPON_ROCKET, launch, aim, Vector3{0, 0, 0});
    assets_.Play("rocket_fire", 0.95f, 0.5f, RandRange(0.94f, 1.04f));
    // The launch itself: a hard whoosh off the rail, plus the backblast out
    // of the rear of the tube that the player's own launcher gets.
    assets_.Play("swing", 0.5f, 0.5f, RandRange(0.7f, 0.85f));
    fx_.MuzzleFlash(hard, aim, 3.4f, 3.0f);
    fx_.MuzzleFlash(hard, Vector3Negate(aim), 2.0f, 2.2f);
    AddFlashLight(hard, Vector3{1.35f, 0.95f, 0.45f}, 300.0f, 0.08f);
    shake_ = fminf(1.0f, shake_ + 0.18f);
    if (heliRockets_ == 0) assets_.Play("lastshot", 0.45f);
    return;
  }

  // --- minigun -------------------------------------------------------------
  if (!held && !pressed) return;
  if (heliBelt_ <= 0) {
    tankReload_ = kHeliMgReload;
    assets_.Play("beep2", 0.35f, 0.5f, 0.7f);
    assets_.Play("reload", 0.6f);
    return;
  }
  --heliBelt_;
  tankCooldown_ = kTankMgCooldown;      // 3 ticks = 1200 rounds a minute
  mgFireTick_ = tick_;

  fx_.MuzzleFlash(hard, aim, 1.7f, 0.7f);
  fx_.EjectCasing(hard, aim, 1.3f);
  AddFlashLight(hard, Vector3{1.2f, 0.85f, 0.4f}, 180.0f, 0.04f);

  const float spread = 0.7f;
  const Vector3 sdir = ForwardFromAngles(lp_.yaw + RandRange(-spread, spread),
                                         lp_.viewPitch() + RandRange(-spread, spread));
  const RayHit wh = world_.Raycast(eye, sdir, 2600.0f);
  float t = wh.hit ? wh.dist : 2600.0f;
  bool head = false, leg = false;
  const int hitP = TracePlayers(eye, sdir, t, &t, &head, &leg);
  const Vector3 end = Vector3Add(eye, Vector3Scale(sdir, t));
  fx_.AddTracer(hard, end, Color{255, 226, 150, 255}, 0.8f, 9000.0f);

  if (hitP >= 0) {
    client_.SendHit(static_cast<uint8_t>(hitP), head ? 44.0f : 24.0f,
                    WEAPON_RIFLE, head);
    fx_.BloodSpray(end, sdir, head ? 7.0f : 5.0f);
    hitMarker_ = 0.22f;
    hitWasHead_ = head;
  } else if (wh.hit) {
    SurfaceImpact(wh.point, wh.normal, wh.brushIndex, GetRandomValue(0, 2) == 0);
  }
}

void Game::TryFireTank(bool pressed, bool held) {
  if (!InTank() || tankCooldown_ > 0 || tankReload_ > 0) return;
  Vehicle& v = vehicles_.list()[drivingVehicle_];

  // The gun points where the turret points, at the pitch you are looking.
  const Vector3 dir = ForwardFromAngles(v.turretDeg, lp_.viewPitch());
  const Vector3 muzzle = vehicles_.GunMuzzle(drivingVehicle_, 4.0f);

  if (tankWeapon_ == 0) {
    if (!pressed) return;                      // the main gun is not automatic
    tankCooldown_ = kTankCannonCooldown;
    assets_.Play("tank_cannon", 1.0f);
    shake_ = fminf(1.5f, shake_ + 0.85f);

    // A big plume off the muzzle: fire core, an eruption of burning gas and a
    // bank of smoke that hangs in front of the tank.
    fx_.MuzzleFlash(muzzle, dir, 7.5f, 9.0f);
    fx_.EmitEruption(Vector3Add(muzzle, Vector3Scale(dir, 16.0f)), 2.4f, 26, 34,
                     190.0f);
    fx_.EmitBulbousCloud(Vector3Add(muzzle, Vector3Scale(dir, 34.0f)), 2.1f, 18,
                         3.4f);
    AddFlashLight(muzzle, Vector3{1.5f, 1.05f, 0.5f}, 620.0f, 0.13f);

    // The shell is traced from clear of our own hull, not from the muzzle.
    // A vehicle puts its bounding box into the world so collision and
    // raycasts treat it as solid, and once the turret is traversed away from
    // the hull that box grows enough to swallow the end of the barrel -- so
    // the first thing the round hit was the tank firing it, and it blew
    // itself up on the shot.
    const Vector3 from = vehicles_.ClearOfHull(drivingVehicle_, muzzle, dir);
    const RayHit wh = world_.Raycast(from, dir, kTankCannonRange);
    float t = wh.hit ? wh.dist : kTankCannonRange;
    bool head = false, leg = false;
    const int hitP = TracePlayers(from, dir, t, &t, &head, &leg);
    const Vector3 end = Vector3Add(from, Vector3Scale(dir, t));

    // Same travelling tracer the rifles use, just much bigger and slower, with
    // a smoke trail laid along its path so the shell reads as a shell.
    fx_.ShellTracer(muzzle, end, dir);
    if (hitP >= 0)
      client_.SendHit(static_cast<uint8_t>(hitP), 250.0f, WEAPON_ROCKET, head);
    // The shell bursts where it lands, whether that is a wall or a person.
    fx_.SpawnExplosion(end, 210.0f, wh.hit && wh.normal.y > 0.4f);
    // A tank round levels what it hits: full lethal radius, and it takes the
    // vehicles nearby with it.
    ApplyLocalBlast(end, 210.0f, 340.0f);
    assets_.PlayAt("explosion", end, lp_.eyePos(), lp_.yaw, 3000.0f, 0.9f);
    return;
  }

  // --- roof-mounted machine gun -------------------------------------------
  if (!held && !pressed) return;
  if (tankMgMag_ <= 0) { tankReload_ = kTankMgReload; return; }
  tankCooldown_ = kTankMgCooldown;
  --tankMgMag_;
  // The clip is a burst of fire, not a single report, so it runs as a loop
  // that is kept alive while rounds are going out and cut the moment they
  // stop -- playing it per round at 20 rounds a second would stack twenty
  // copies on top of each other.
  mgFireTick_ = tick_;

  const Vector3 mgMuzzle = vehicles_.GunMuzzle(drivingVehicle_, 12.0f);
  fx_.MuzzleFlash(mgMuzzle, dir, 1.5f, 0.6f);
  fx_.EjectCasing(mgMuzzle, dir, 1.4f);
  AddFlashLight(mgMuzzle, Vector3{1.2f, 0.85f, 0.4f}, 170.0f, 0.045f);

  // The gun is aimed from the eye, not from the muzzle, so every round goes
  // exactly where the crosshair is -- the tracer just leaves from the barrel
  // and converges on the same point.
  const Vector3 eye = lp_.eyePos();
  const float spread = 0.5f;
  const Vector3 sdir = ForwardFromAngles(lp_.yaw + RandRange(-spread, spread),
                                         lp_.viewPitch() + RandRange(-spread, spread));
  const RayHit wh = world_.Raycast(eye, sdir, 2200.0f);
  float t = wh.hit ? wh.dist : 2200.0f;
  bool head = false, leg = false;
  const int hitP = TracePlayers(eye, sdir, t, &t, &head, &leg);
  const Vector3 end = Vector3Add(eye, Vector3Scale(sdir, t));
  fx_.AddTracer(mgMuzzle, end, Color{255, 232, 160, 255}, 0.7f, 9000.0f);

  if (hitP >= 0) {
    client_.SendHit(static_cast<uint8_t>(hitP), head ? 42.0f : 22.0f,
                    WEAPON_RIFLE, head);
    fx_.BloodSpray(end, sdir, head ? 7.0f : 5.0f);
    hitMarker_ = 0.22f;
    hitWasHead_ = head;
    assets_.Play(head ? "headshot" : "hitmark", head ? 0.5f : 0.28f, 0.5f,
                 head ? 1.0f : 1.4f);
  } else if (wh.hit) {
    SurfaceImpact(wh.point, wh.normal, wh.brushIndex, GetRandomValue(0, 2) == 0);
  }
}

const Texture2D* Game::SkinFor(int playerId) const {
  // Enemies are the procedural SWAT figure now, not GTJ3D sprites, so body
  // parts fall back to their own SWAT-palette colours.
  (void)playerId;
  return nullptr;
}

void Game::DoHitscan() {
  const WeaponDef& d = Weapon(lp_.arsenal.current);
  const Vector3 origin = lp_.eyePos();
  const float spread = lp_.CurrentSpread();
  // One muzzle for the whole trigger pull; every pellet's tracer leaves from
  // exactly here and spears out to wherever that pellet actually goes.
  const Vector3 muzzle = MuzzlePos();
  bool anyHead = false;

  // Sniper and shotgun belch; the rest get a modest puff.
  const float smokeMul = (lp_.arsenal.current == WEAPON_SNIPER) ? 2.6f
                       : (lp_.arsenal.current == WEAPON_SHOTGUN) ? 2.2f : 1.0f;
  fx_.MuzzleFlash(muzzle, lp_.aimDir(),
                  d.smokeScale > 0.0f ? d.smokeScale : 1.2f, smokeMul);
  // Every firearm throws its brass. One case per trigger pull, not one per
  // pellet -- a shotgun ejects a single hull however much shot is in it --
  // and sized to the round it came out of.
  if (d.mode != FIRE_MELEE && d.mode != FIRE_PLACE) {
    const int w = lp_.arsenal.current;
    const float caseScale =
        (w == WEAPON_SHOTGUN || w == WEAPON_SUPERSHOTGUN) ? 2.1f
        : (w == WEAPON_SNIPER) ? 1.9f
        : (w == WEAPON_RIFLE) ? 1.5f
        : (w == WEAPON_SMG) ? 1.1f : 1.2f;
    fx_.EjectCasing(muzzle, lp_.aimDir(), caseScale);
    // The super shotgun breaks open on two barrels, so it throws two.
    if (w == WEAPON_SUPERSHOTGUN) fx_.EjectCasing(muzzle, lp_.aimDir(), 2.1f);
  }
  // A bright warm flash that briefly lights everything around the muzzle.
  AddFlashLight(muzzle, Vector3{1.27f, 0.88f, 0.42f},
                150.0f + smokeMul * 110.0f, 0.055f + smokeMul * 0.02f);

  for (int p = 0; p < d.pellets; ++p) {
    const float yaw = lp_.yaw + RandRange(-spread, spread);
    const float pitch = lp_.viewPitch() + RandRange(-spread, spread);
    const Vector3 dir = ForwardFromAngles(yaw, pitch);

    const RayHit wh = world_.Raycast(origin, dir, d.range);
    const float wallT = wh.hit ? wh.dist : d.range;

    float playerT = wallT;
    bool head = false, leg = false;
    const int hit = TracePlayers(origin, dir, wallT, &playerT, &head, &leg);

    const Vector3 end = Vector3Add(origin, Vector3Scale(dir, playerT));
    fx_.AddTracer(muzzle, end, Color{255, 226, 150, 255}, 0.5f,
                  BulletSpeedFor(lp_.arsenal.current));

    if (hit >= 0) {
      const float mult = head ? kHeadMult : (leg ? kLegMult : 1.0f);
      const float fall = Clampf(1.0f - 0.4f * ((playerT / d.range) - 0.45f) / 0.55f,
                                0.6f, 1.0f);
      const float dmg = d.damage * mult * fall;
      client_.SendHit(static_cast<uint8_t>(hit), dmg, lp_.arsenal.current, head);
      fx_.BloodSpray(end, dir, head ? 7.0f : 5.0f);
      // ...and it lands on whatever is behind them.
      SplatterBlood(end, dir, head ? 6.0f : 4.0f);
      hitMarker_ = 0.25f;
      anyHead = anyHead || head;
      assets_.Play(head ? "headshot" : "hitmark", head ? 0.55f : 0.32f, 0.5f,
                   head ? 1.0f : 1.4f);
      assets_.PlayAt("bullethit", end, lp_.eyePos(), lp_.yaw, 900.0f, 0.5f);
    } else {
      // Rounds that land on a vehicle hurt it. Small arms ring off armour --
      // a tank is not going down to a rifle -- but bodywork is bodywork, so a
      // van or a car takes it, and a gunship's skin is thin.
      if (wh.hit) {
        int vi = -1;
        const float armourScale = HurtsTank(lp_.arsenal.current) ? 1.0f : 0.16f;
        if (vehicles_.DamageByBrush(world_, wh.brushIndex,
                                    d.damage * armourScale, &vi)) {
          const Vehicle& v = vehicles_.list()[vi];
          hitMarker_ = 0.18f;
          assets_.Play("hitmark", 0.22f, 0.5f, 1.7f);
          if (v.life <= 0.0f) {
            fx_.SpawnExplosion(Vector3{v.pos.x, v.pos.y + 16.0f, v.pos.z},
                               190.0f, true);
            assets_.PlayAt("explosion", v.pos, lp_.eyePos(), lp_.yaw, 3000.0f,
                           0.95f);
          }
        }
      }
      // The impact SFX and mark land when the round arrives, not when it left.
      TrackBullet(muzzle, dir, wallT, wh.hit, wh.point, wh.normal, wh.brushIndex,
                  lp_.arsenal.current, true);
    }
  }
  if (hitMarker_ > 0.0f) hitWasHead_ = anyHead;
}

void Game::DoMelee() {
  const WeaponDef& d = Weapon(WEAPON_FISTS);
  const Vector3 origin = lp_.eyePos();
  const Vector3 dir = lp_.aimDir();

  const RayHit wh = world_.Raycast(origin, dir, d.range);
  const float wallT = wh.hit ? wh.dist : d.range;
  float t = wallT;
  bool head = false, leg = false;
  const int hit = TracePlayers(origin, dir, wallT, &t, &head, &leg);

  if (hit >= 0) {
    const RemotePlayer* ps = client_.players();
    const Vector3 theirFwd = FlatForward(ps[hit].cur.yaw);
    const Vector3 mine = FlatForward(lp_.yaw);
    const bool back = Vector3DotProduct(theirFwd, mine) > 0.55f;
    const float dmg = back ? 95.0f : d.damage * (head ? 1.6f : 1.0f);
    client_.SendHit(static_cast<uint8_t>(hit), dmg, WEAPON_FISTS, head);
    fx_.BloodPuff(Vector3Add(origin, Vector3Scale(dir, t)), dir, 6.0f);
    hitMarker_ = 0.3f;
    hitWasHead_ = back || head;
    assets_.Play("punch", 0.7f);
  } else if (wh.hit) {
    SurfaceImpact(wh.point, wh.normal, wh.brushIndex, false);
  }
}

void Game::DoPlace() {
  const int w = lp_.arsenal.current;
  const Vector3 eye = lp_.eyePos();
  const Vector3 dir = lp_.aimDir();

  if (w == WEAPON_MINE) {
    const Vector3 ahead =
        Vector3Add(lp_.pos, Vector3Scale(FlatForward(lp_.yaw), 20.0f));
    const RayHit down = world_.Raycast(Vector3{ahead.x, lp_.pos.y + 12.0f, ahead.z},
                                       Vector3{0, -1, 0}, 80.0f);
    const Vector3 at = down.hit ? Vector3Add(down.point, Vector3{0, 2.0f, 0})
                                : Vector3{ahead.x, lp_.pos.y + 2.0f, ahead.z};
    client_.SendFire(static_cast<uint8_t>(w), at, Vector3{0, 1, 0}, Vector3{0, 0, 0});
    SetMessage("Mine armed", 1.4f);
    return;
  }

  const WeaponDef& d = Weapon(WEAPON_TRIPFLARE);
  const RayHit wh = world_.Raycast(eye, dir, 220.0f);
  if (!wh.hit) {
    SetMessage("No surface in range", 1.4f);
    return;
  }
  const Vector3 at = Vector3Add(wh.point, Vector3Scale(wh.normal, 3.0f));
  Vector3 beamDir;
  if (fabsf(wh.normal.y) > 0.7f) {
    beamDir = FlatRight(lp_.yaw);
  } else {
    beamDir = Vector3Normalize(Vector3CrossProduct(wh.normal, Vector3{0, 1, 0}));
  }
  const RayHit beamHit = world_.Raycast(at, beamDir, d.range);
  const Vector3 end = beamHit.hit
                          ? beamHit.point
                          : Vector3Add(at, Vector3Scale(beamDir, d.range));
  client_.SendFire(static_cast<uint8_t>(w), at, wh.normal, end);
  SetMessage("Tripflare set", 1.4f);
}

void Game::TryFire(bool pressed, bool held) {
  Arsenal& a = lp_.arsenal;
  const WeaponDef& d = Weapon(a.current);
  const bool wants = (d.mode == FIRE_AUTO) ? held : pressed;
  if (!wants) return;

  if (!a.CanFire()) {
    if (pressed && a.NeedsReload()) a.BeginReload();
    else if (pressed && d.magSize > 0 && a.cur().mag == 0 && a.cur().reserve == 0)
      assets_.Play("magclick", 0.45f);
    return;
  }

  a.cur().cooldown = d.cooldown;
  if (d.mode != FIRE_MELEE) {
    if (d.magSize > 0) a.cur().mag--;
    else a.cur().reserve--;
  }
  // Dry-fire warning on the last round, straight out of Naval Command.
  if (d.magSize > 0 && a.cur().mag == 0) assets_.Play("lastshot", 0.4f);

  // GTJ3D's gun_reload() ran the animation across the whole cooldown. That is
  // wrong for a slow weapon -- the sniper would hold its muzzle flash for a
  // second and a half. Each weapon now names its own animation length, so the
  // sniper flashes exactly as fast as the rifle and then sits idle.
  // fireAnimTicks of 0 means this weapon does not animate when fired -- the
  // pistol holds its idle frame rather than kicking up the screen.
  const SpriteSheet& sheet = assets_.Sprite(d.viewmodel);
  vmAnimFrames_ = sheet.valid() ? (int)sheet.frames.size() : 1;
  vmAnimT_ = 0.0f;
  vmAnimDur_ = static_cast<float>(d.fireAnimTicks) * kTickDt;
  vmFrame_ = 0;
  vmRecoilT_ = 1.0f;

  const Vector3 eye = lp_.eyePos();
  const Vector3 dir = lp_.aimDir();
  const Vector3 muzzle = MuzzlePos();

  switch (d.mode) {
    case FIRE_MELEE:
      DoMelee();
      client_.SendFire(a.current, eye, dir, Vector3{0, 0, 0});
      break;
    case FIRE_SEMI:
    case FIRE_AUTO:
      DoHitscan();
      client_.SendFire(a.current, eye, dir, Vector3{0, 0, 0});
      break;
    case FIRE_PROJECTILE:
      // A launch tube throws a big flash and a lot of smoke.
      fx_.MuzzleFlash(muzzle, dir, d.smokeScale > 0 ? d.smokeScale : 2.0f,
                      a.current == WEAPON_ROCKET ? 3.4f : 1.0f);
      if (a.current == WEAPON_ROCKET) {
        // Backblast out of the rear of the tube.
        fx_.EmitBurst(Vector3Subtract(eye, Vector3Scale(dir, 6.0f)),
                      Vector3Scale(dir, -60.0f), 10, 0.9f, 4.0f, 26.0f, 0.35f,
                      0.0f, 6.0f, 0.3f);
      }
      client_.SendFire(a.current, Vector3Add(eye, Vector3Scale(dir, 14.0f)), dir,
                       Vector3{0, 0, 0});
      break;
    case FIRE_PLACE:
      DoPlace();
      break;
  }

  lp_.ApplyRecoil(d.recoil);
  // The sniper is deliberately the loudest thing on the map.
  const float vol = (d.mode == FIRE_MELEE)        ? 0.5f
                  : (a.current == WEAPON_SNIPER)  ? 1.0f
                  : (a.current == WEAPON_SHOTGUN) ? 0.95f
                                                  : 0.85f;
  // One report per shot -- the sniper is just the rifle clip pitched down and
  // played loud, with no explosion layered underneath.
  assets_.Play(d.fireSound, vol, 0.5f, d.firePitch * RandRange(0.97f, 1.03f));
  if (d.mode == FIRE_PROJECTILE) shake_ = 0.6f;
}

// --------------------------------------------------------------------- events

void Game::PushKillFeed(const std::string& s) {
  killFeed_.push_front(s);
  while (killFeed_.size() > 5) killFeed_.pop_back();
}

void Game::SetMessage(const std::string& s, float seconds) {
  message_ = s;
  messageTime_ = seconds;
}

void Game::ProcessEvents() {
  const int me = client_.myId();
  const RemotePlayer* ps = client_.players();
  auto nameOf = [&](int id) -> std::string {
    if (id < 0 || id >= kMaxPlayers) return "someone";
    if (id == me) return "You";
    return ps[id].name.empty() ? "player" : ps[id].name;
  };

  for (const NetEvent& e : client_.TakeEvents()) {
    switch (e.type) {
      case EV_FIRE: {
        renderer_.PokeShotRecoil(e.a);
        if (e.a == me) break;
        const WeaponDef& d = Weapon(e.weapon);
        const Vector3 o{e.x, e.y, e.z};
        const Vector3 dir{e.dx, e.dy, e.dz};
        assets_.PlayAt(d.fireSound, o, lp_.eyePos(), lp_.yaw, 1900.0f, 0.9f,
                       d.firePitch * RandRange(0.97f, 1.03f));
        if (d.mode == FIRE_SEMI || d.mode == FIRE_AUTO) {
          const RayHit wh = world_.Raycast(o, dir, d.range);
          const float t = wh.hit ? wh.dist : d.range;
          // Other players' tracers leave their muzzle too, not their eye. We
          // only have their eye position on the wire, so push forward by the
          // length of the weapon.
          const Vector3 muzzle = Vector3Add(o, Vector3Scale(dir, 14.0f));
          fx_.AddTracer(muzzle, Vector3Add(o, Vector3Scale(dir, t)),
                        Color{255, 210, 130, 220}, 0.4f,
                        BulletSpeedFor(e.weapon));
          const float sm = (e.weapon == WEAPON_SNIPER) ? 2.6f
                         : (e.weapon == WEAPON_SHOTGUN) ? 2.2f : 1.0f;
          fx_.MuzzleFlash(muzzle, dir, 1.4f, sm);
          AddFlashLight(muzzle, Vector3{1.27f, 0.88f, 0.42f},
                        150.0f + sm * 110.0f, 0.055f + sm * 0.02f);
          // The whoosh and the impact are both driven by the round's flight,
          // so a bullet that buries itself in a wall short of you never
          // whooshes -- you just hear it hit.
          TrackBullet(muzzle, dir, t, wh.hit, wh.point, wh.normal, wh.brushIndex,
                      e.weapon, false);
        }
        break;
      }
      case EV_BLAST: {
        const Vector3 p{e.x, e.y, e.z};
        const float ground = world_.GroundHeight(p.x, p.z, 6.0f, p.y + 40.0f);
        fx_.SpawnExplosion(p, e.value, (p.y - ground) < e.value * 0.45f);
        AddFlashLight(p, Vector3{1.61f, 1.01f, 0.40f}, e.value * 4.5f, 0.45f);
        // Exactly one report per blast -- layering two made every explosion
        // sound doubled -- and it is GTJ3D's own `snd_explosion` for rockets
        // and grenades alike, which is the sound `obj_explosion_effect`
        // assigned to `control.play_sound`. The Naval Command clips that were
        // here are MP3s: their decoder padding puts a few milliseconds of
        // silence in front of the transient, so the bang arrived after the
        // fireball had already bloomed. A WAV starts on the sample it says it
        // does, so the report lands on the same frame as the flash.
        const char* boom = (e.b == ENT_GRENADE || e.b == ENT_ROCKET)
                               ? "explosion"
                               : (e.value > 115.0f ? "bigboom" : "explosion3");
        assets_.PlayAt(boom, p, lp_.eyePos(), lp_.yaw, 2600.0f, 1.0f,
                       RandRange(0.94f, 1.04f));
        const float dist = Vector3Distance(p, lp_.eyePos());
        if (dist < e.value * 3.0f)
          shake_ = fmaxf(shake_, 4.0f * (1.0f - dist / (e.value * 3.0f)));
        break;
      }
      case EV_HIT: {
        const Vector3 p{e.x, e.y, e.z};
        if (e.b != me) {
          const Vector3 sd{e.dx, e.dy, e.dz};
          const Vector3 dir =
              Vector3LengthSqr(sd) > 0.01f ? sd : Vector3{0, 1, 0};
          fx_.BloodSpray(p, dir, 4.5f);
          // Every hit throws blood onto the geometry behind it, not just the
          // ones we landed ourselves -- a firefight across the street should
          // leave the wall marked whoever was shooting.
          SplatterBlood(p, dir, 4.0f);
        }
        if (e.a == me && e.b != me) {
          hitMarker_ = 0.25f;
          hitWasHead_ = (e.flags & 1) != 0;
        }
        // Our own fall damage is echoed back to us as a hit, but the flash,
        // the shake and the grunt were already played the instant we landed.
        if (e.b == me && e.weapon != kDeathByFalling) {
          // Remembered so that, if we are in a vehicle, the redirected damage
          // knows whether it was something a tank's armour can shrug off.
          lastHitWeapon_ = e.weapon;
          // Capped below 1 so sustained fire pulses rather than painting the
          // whole screen red and staying there.
          damageFlash_ = fminf(0.8f, damageFlash_ + e.value / 60.0f);
          assets_.Play(GetRandomValue(0, 1) ? "hurt" : "hurt2", 0.7f);
        }
        break;
      }
      case EV_DEATH: {
        const int victim = e.a, killer = e.b;
        char buf[128];
        if (e.weapon == kDeathByFalling) {
          snprintf(buf, sizeof(buf), "%s hit the ground", nameOf(victim).c_str());
        } else if (victim == killer) {
          snprintf(buf, sizeof(buf), "%s blew %s up", nameOf(victim).c_str(),
                   victim == me ? "yourself" : "themselves");
        } else {
          snprintf(buf, sizeof(buf), "%s  [%s]%s  %s", nameOf(killer).c_str(),
                   Weapon(e.weapon).hudName, (e.flags & 1) ? " HS" : "",
                   nameOf(victim).c_str());
        }
        PushKillFeed(buf);
        // Story mode's running total. Only enemies you actually put down --
        // one blowing itself up on its own grenade is not your kill.
        if (storyMode_ && story_.active() && killer == me && victim != me)
          story_.AddKill();

        // ---- ragdoll / gib ---------------------------------------------
        // The server tells us the direction of the killing blow and how high
        // up the body it landed, so the corpse folds and flies accordingly.
        // Skipped for your own death: the camera stays at your eye position,
        // so your own corpse would spawn wrapped around the lens.
        if (victim != me) {
          const Vector3 feet{e.x, e.y, e.z};
          const float heightFrac = Clampf(e.value, 0.05f, 1.0f);
          const float bodyH = kPlayerHeight;
          Vector3 shotDir{e.dx, e.dy, e.dz};
          if (Vector3LengthSqr(shotDir) < 0.01f) shotDir = Vector3{0, 1, 0};
          shotDir = Vector3Normalize(shotDir);
          const Vector3 wound =
              Vector3Add(feet, Vector3{0, bodyH * heightFrac, 0});

          const bool explosive =
              e.weapon == WEAPON_ROCKET || e.weapon == WEAPON_GRENADE ||
              e.weapon == WEAPON_MINE || e.weapon == WEAPON_TRIPFLARE;

          // A death paints the place it happened. Far more of it than a hit,
          // and thrown in every direction as well as down the shot line,
          // because a body coming apart does not respect the exit path. An
          // explosive one goes everywhere.
          SplatterBlood(wound, shotDir, explosive ? 16.0f : 10.0f);
          SplatterBlood(wound, Vector3{0.0f, -1.0f, 0.0f}, 6.0f);
          if (explosive) {
            SplatterBlood(wound, Vector3{0.0f, 1.0f, 0.0f}, 8.0f);
            SplatterBlood(wound, Vector3Negate(shotDir), 8.0f);
          }
          const float force = explosive ? 320.0f
                            : (e.weapon == WEAPON_SNIPER   ? 200.0f
                             : e.weapon == WEAPON_SHOTGUN  ? 175.0f
                                                           : 120.0f);
          float yaw = 0.0f;
          Color team{150, 150, 158, 255};
          if (victim >= 0 && victim < kMaxPlayers && ps[victim].active) {
            yaw = ps[victim].cur.yaw;
            team = (ps[victim].cur.team % 2) ? Color{198, 112, 56, 255}
                                             : Color{78, 132, 196, 255};
          }
          fx_.SpawnCorpse(feet, yaw, bodyH, team, SkinFor(victim), wound,
                          shotDir, force, (e.flags & 1) != 0, explosive);
          if (explosive) {
            assets_.PlayAt("explosion2", wound, lp_.eyePos(), lp_.yaw, 1800.0f,
                           0.7f, RandRange(0.9f, 1.1f));
          }
        }

        const Vector3 p{e.x, e.y + 10.0f, e.z};
        assets_.PlayAt(GetRandomValue(0, 1) ? "death" : "death2", p, lp_.eyePos(),
                       lp_.yaw, 1600.0f, 0.8f);

        if (victim == me) {
          lp_.dead = true;
          respawnIn_ = kRespawnTime;
          damageFlash_ = 1.0f;
        } else if (killer == me) {
          SetMessage((e.flags & 1) ? "HEADSHOT" : "ELIMINATED", 1.4f);
          assets_.Play("good", 0.5f);
        }
        break;
      }
      case EV_SPAWN: {
        if (e.a == me) {
          lp_.Reset(Vector3{e.x, e.y, e.z}, e.value);
          respawnIn_ = 0.0f;
          damageFlash_ = 0.0f;
          vmAnimDur_ = 0.0f;
          assets_.Play("spawn", 0.5f);
        }
        break;
      }
      default:
        break;
    }
  }
}

void Game::DebugReport() const {
  int alive = 0;
  const RemotePlayer* ps = client_.players();
  for (int i = 0; i < kMaxPlayers; ++i) if (ps[i].active) ++alive;
  TraceLog(LOG_INFO,
           "DEBUG: pos=(%.1f, %.1f, %.1f) yaw=%.1f onGround=%d speed=%.2f "
           "hp=%.0f weapon=%s players=%d ents=%d particles=%d decals=%d "
           "weather=%s storm=%.2f ping=%.0fms",
           lp_.pos.x, lp_.pos.y, lp_.pos.z, lp_.yaw, (int)lp_.onGround, lp_.speed,
           lp_.health, Weapon(lp_.arsenal.current).name, alive,
           (int)client_.entities().size(), fx_.particleCount(), fx_.decalCount(),
           sky_.bandName(), sky_.storm(), client_.ping());
}

// ---------------------------------------------------------------------- draw

Camera3D Game::BuildCamera() const {
  Camera3D cam{};
  Vector3 eye = lp_.eyePos();
  if (shake_ > 0.0f) {
    eye.x += RandRange(-shake_, shake_);
    eye.y += RandRange(-shake_, shake_);
    eye.z += RandRange(-shake_, shake_);
  }
  cam.position = eye;
  cam.target = Vector3Add(eye, lp_.aimDir());
  cam.up = Vector3{0.0f, 1.0f, 0.0f};
  const WeaponDef& d = Weapon(lp_.arsenal.current);
  cam.fovy = kFovY + (d.zoomFov - kFovY) * (d.canZoom ? lp_.zoomT : 0.0f);
  cam.projection = CAMERA_PERSPECTIVE;
  return cam;
}

void Game::DrawGame() {
  const Camera3D cam = BuildCamera();
  const int W = GetScreenWidth(), H = GetScreenHeight();

  ClearBackground(sky_.horizonColor());
  sky_.DrawSky(cam, W, H);

  const float fogEnd = world_.fogEnd() * sky_.fogScale();
  const float fogStart = fminf(world_.fogStart(), fogEnd * 0.35f);

  // Muzzle flashes and explosions actually light the geometry around them.
  renderer_.ClearLights();
  for (const FlashLight& f : flashLights_) {
    const float k = 1.0f - Clampf(f.age / f.life, 0.0f, 1.0f);
    // Squared falloff over the life so the flash snaps off rather than fading.
    renderer_.AddLight(f.pos, f.radius, Vector3Scale(f.color, k * k));
  }

  renderer_.BeginWorld(cam, sky_.fogColor(), fogStart, fogEnd, sky_.ambientColor());
  renderer_.DrawWorld(world_, assets_, cam.position, fogEnd);
  renderer_.DrawPlayers(client_, client_.myId(), assets_, now_ - 0.08, cam);
  renderer_.DrawEntities(client_.entities(), assets_, cam);
  // Vehicles are glTF bodies, drawn inside the fog pass so they sit in the
  // same atmosphere as the geometry around them. The vehicle you are in is
  // drawn like any other, so its bodywork is there underneath you rather than
  // the world appearing to carry you along on nothing -- except for a car,
  // where the glTF body is swapped for GTJ3D's own interior shell, which is
  // what obj_car drew once you were at the wheel.
  // The car you are driving is swapped for GTJ3D's own interior: obj_car's
  // `interior_model`, the taller cabin wearing frame 1 of its skin, with its
  // own steering wheel on its own quad in front of you.
  const bool inCar = drivingVehicle_ >= 0 && !lp_.dead && enterTimer_ == 0 &&
                     VehicleSystem::HasGtjShell(
                         vehicles_.list()[drivingVehicle_].kind) &&
                     assets_.haveCarSkins();
  vehicles_.Draw(assets_, cam.position, inCar ? drivingVehicle_ : -1);
  // Last, because the cabin's glass and the steering wheel are transparent
  // and have to composite over everything they are in front of -- which,
  // from the driver's seat, is the entire rest of the world.
  if (inCar)
    vehicles_.DrawGtjShell(assets_, drivingVehicle_, cam.position,
                           /*interior=*/true, driveTurning_);
  if (debugShowcase_) {
    // A static enemy 46 units ahead, turned side-on, for model inspection.
    const Vector3 fwd = FlatForward(lp_.yaw);
    const Vector3 at = Vector3Add(lp_.pos, Vector3Scale(fwd, 46.0f));
    const float g = world_.GroundHeight(at.x, at.z, 8.0f, at.y + 60.0f);
    rlPushMatrix();
    rlTranslatef(at.x, g, at.z);
    rlRotatef(lp_.yaw + 235.0f, 0.0f, 1.0f, 0.0f);
    DrawSwatFigure(kPlayerHeight, WHITE);
    rlPopMatrix();
  }
  renderer_.EndWorld();

  // Rain goes down before the effects so smoke and fire are never dimmed or
  // cut into by the weather overlay -- explosions get display priority over
  // everything except the HUD.
  sky_.DrawRain(cam, W, H);

  // Particles are drawn outside the fog shader so smoke keeps its own colour.
  BeginMode3D(cam);
  fx_.Draw(cam);
  EndMode3D();

  sky_.DrawLightningFlash(W, H);

  const HudTransform hud = HudTransform::For(W, H);
  renderer_.DrawNameTags();

  // Reload progress for the viewmodel animation. Shell-fed weapons reload one
  // round at a time, so their cycle is per shell rather than per magazine.
  float reloadT = 0.0f;
  {
    const WeaponSlot& s = lp_.arsenal.cur();
    const WeaponDef& d = Weapon(lp_.arsenal.current);
    if (s.reloading && d.reloadTicks > 0) {
      reloadT = 1.0f - Clampf((float)s.reloadTimer / (float)d.reloadTicks,
                              0.0f, 1.0f);
    }
  }
  // In a car you are holding a steering wheel, not a weapon -- GTJ3D swapped
  // the same way, drawing spr_steering_wheel in front of the driver.
  if (drivingVehicle_ >= 0 && !lp_.dead) {
    const Vehicle& v = vehicles_.list()[drivingVehicle_];
    // Sitting at the window line means looking through glass. A very light
    // cool wash sells that without dimming what you are trying to shoot at.
    if (v.kind == VEH_HELI) {
      // Artificial cockpit. The fuselage is one shell with no interior, so
      // rather than sit inside untextured geometry the canopy is drawn as a
      // frame around the view: glass tint, a windscreen bar down the middle,
      // pillars at the corners and a coaming across the bottom. It reads as
      // being in the cockpit while leaving the middle of the screen clear.
      const Color glass{168, 200, 222, 22};
      const Color frame{26, 28, 32, 240};
      DrawRectangle(0, 0, W, H, glass);
      const int bar = (int)(W * 0.010f) + 2;
      DrawRectangle(W / 2 - bar / 2, 0, bar, (int)(H * 0.30f), frame);
      DrawRectangle(W / 2 - bar / 2, (int)(H * 0.72f), bar, H, frame);
      // Corner pillars, slanted in by drawing a stack of narrowing bars.
      for (int i = 0; i < 26; ++i) {
        const float t = i / 25.0f;
        const int y = (int)(H * (0.02f + t * 0.30f));
        const int w2 = (int)(W * (0.070f - t * 0.052f));
        const int h2 = (int)(H * 0.014f) + 1;
        DrawRectangle(0, y, w2, h2, frame);
        DrawRectangle(W - w2, y, w2, h2, frame);
      }
      // Instrument coaming along the bottom of the glass.
      DrawRectangle(0, (int)(H * 0.855f), W, H, Color{22, 24, 27, 245});
      DrawRectangle(0, (int)(H * 0.855f), W, 3, Color{78, 84, 92, 255});
    } else if (v.kind != VEH_TANK && !inCar) {
      // Cars wearing GTJ3D's shell have real glazing in front of them, drawn
      // on obj_car's own window panels. This flat wash is only for the ones
      // that did not get it -- the supercar, or any car if the skins were
      // never staged.
      DrawRectangle(0, 0, W, H, Color{176, 206, 226, 22});
    }
    if (v.kind != VEH_TANK && v.kind != VEH_HELI && enterTimer_ == 0 &&
        !inCar) {
      // Same fallback: obj_car's own wheel is drawn in the world by
      // VehicleSystem::DrawGtjShell when the shell is there.
      // A little vertical shove from the road, scaled by speed.
      const float bump = sinf((float)now_ * 26.0f) * 2.2f *
                         Clampf(fabsf(v.speed) / 8.0f, 0.0f, 1.0f);
      renderer_.DrawSteeringWheel(assets_, driveTurning_, v.wheelAngle, bump,
                                  hud);
    }
  } else {
    renderer_.DrawViewmodel(assets_, lp_.arsenal.current, vmFrame_,
                            lp_.bobPhase, lp_.bobAmount, lp_.zoomT, lp_.dead,
                            reloadT, vmRecoilT_, hud);
  }

  HudInfo info;
  info.health = lp_.health;
  info.armor = lp_.armor;
  info.weapon = lp_.arsenal.current;
  info.mag = lp_.arsenal.cur().mag;
  info.reserve = lp_.arsenal.cur().reserve;
  info.reloading = lp_.arsenal.cur().reloading;
  info.dead = lp_.dead;
  info.respawnIn = respawnIn_;
  info.spreadDeg = lp_.CurrentSpread();
  info.hitMarker = hitMarker_;
  info.hitWasHead = hitWasHead_;
  info.damageFlash = damageFlash_;
  info.zoomT = lp_.zoomT;
  info.fps = GetFPS();
  info.ping = singlePlayer_ ? 0.0f : client_.ping();
  info.singlePlayer = singlePlayer_;
  info.message = message_;
  info.messageTime = messageTime_;
  info.inVehicle = drivingVehicle_ >= 0;
  info.killFeed.assign(killFeed_.begin(), killFeed_.end());
  renderer_.DrawHud(assets_, info, hud, W, H);

  // --- story mode ---------------------------------------------------------
  if (storyMode_ && story_.active()) {
    // A permanent strip at the top: which wave, and what is left of it.
    const StoryWave& w = story_.spec();
    const int left = EnemiesAlive() + story_.reinforcementsLeft();
    char strip[160];
    snprintf(strip, sizeof(strip),
             "WAVE %d%s      ENEMIES %d      KILLED %d      LIVES %d",
             story_.wave(), w.heavy ? "  (HEAVY)" : "", left, story_.killed(),
             story_.lives());
    const int fs = (int)(21 * hud.scale);
    const int sw = MeasureText(strip, fs);
    DrawRectangle(W / 2 - sw / 2 - 16, 6, sw + 32, fs + 12,
                  Color{0, 0, 0, 120});
    DrawText(strip, W / 2 - sw / 2, 12, fs,
             w.heavy ? Color{255, 170, 110, 245} : Color{225, 232, 240, 235});

    // ...and the big announcement in the middle when there is one.
    const std::string big = story_.banner();
    if (!big.empty()) {
      const bool failed = story_.phase() == Story::Phase::Failed;
      const int bs = (int)(52 * hud.scale);
      const int bw = MeasureText(big.c_str(), bs);
      const int by = (int)(H * (failed ? 0.36f : 0.30f));
      DrawText(big.c_str(), W / 2 - bw / 2 + 2, by + 2, bs,
               Color{0, 0, 0, 170});
      DrawText(big.c_str(), W / 2 - bw / 2, by, bs,
               failed ? Color{235, 90, 80, 255} : Color{255, 220, 120, 255});
      const std::string sub2 = story_.subBanner();
      if (!sub2.empty()) {
        const int ss = (int)(19 * hud.scale);
        const int sw2 = MeasureText(sub2.c_str(), ss);
        DrawText(sub2.c_str(), W / 2 - sw2 / 2, by + bs + 8, ss,
                 Color{205, 212, 222, 230});
      }
      if (failed) {
        const char* again = "ESC for the menu";
        const int as = (int)(20 * hud.scale);
        DrawText(again, W / 2 - MeasureText(again, as) / 2,
                 by + bs + 52, as, Color{170, 178, 190, 220});
      }
    }
  }

  // Vehicle prompt and speedo, under the crosshair where the eye already is.
  if (nearVehicle_ >= 0 && drivingVehicle_ < 0 && !lp_.dead) {
    const Vehicle& v = vehicles_.list()[nearVehicle_];
    char line[96];
    snprintf(line, sizeof(line), "[E]  drive the %s",
             VehicleInfo(v.kind).name);
    const int fs = (int)(20 * hud.scale);
    DrawText(line, W / 2 - MeasureText(line, fs) / 2, (int)(H * 0.60f), fs,
             Color{255, 232, 150, 235});
  } else if (drivingVehicle_ >= 0) {
    const Vehicle& v = vehicles_.list()[drivingVehicle_];
    char line[96];
    // GTJ's speed is units per 60 Hz step; x60 puts it in units per second,
    // which reads as a sane-looking km/h once divided by the 11 units/metre
    // the rest of the game works in.
    if (v.kind == VEH_HELI) {
      const float agl =
          v.pos.y - world_.GroundHeight(v.pos.x, v.pos.z, 12.0f, v.pos.y + 30.0f);
      if (tankWeapon_ == 0) {
        snprintf(line, sizeof(line), "Gunship  %3.0f m AGL   [1] MINIGUN  %d%s",
                 agl / kUnitsPerMetre, heliBelt_,
                 tankReload_ > 0 ? "   RELOADING" : "");
      } else {
        snprintf(line, sizeof(line), "Gunship  %3.0f m AGL   [2] ROCKETS  %d%s",
                 agl / kUnitsPerMetre, heliRockets_,
                 tankReload_ > 0 ? "   RELOADING" : "");
      }
    } else if (v.kind == VEH_TANK) {
      // The tank flies its own armament state: which gun, and what is left.
      if (tankWeapon_ == 0) {
        snprintf(line, sizeof(line), "Tank   [1] 120mm MAIN GUN   %s",
                 tankCooldown_ > 0 ? "LOADING" : "READY");
      } else {
        snprintf(line, sizeof(line), "Tank   [2] ROOF MG   %d / %d%s",
                 tankMgMag_, kTankMgMagSize,
                 tankReload_ > 0 ? "   RELOADING" : "");
      }
    } else {
      snprintf(line, sizeof(line), "%s   %3.0f km/h   %3.0f%%",
               VehicleInfo(v.kind).name,
               fabsf(v.speed) * 60.0f / kUnitsPerMetre * 3.6f,
               fmaxf(0.0f, v.life / VehicleInfo(v.kind).maxLife * 100.0f));
    }
    const int fs = (int)(20 * hud.scale);
    DrawText(line, W / 2 - MeasureText(line, fs) / 2, (int)(H * 0.86f), fs,
             enterTimer_ > 0 ? Color{180, 186, 196, 200}
                             : Color{255, 232, 150, 225});
  }

  if (showScores_) renderer_.DrawScoreboard(client_, client_.myId(), hud);
  if (devOverlay_ || devHelp_) DrawDevOverlay();

  if (!mouseCaptured_) {
    const char* t = "Click to capture the mouse   -   Esc for the menu";
    const int w = MeasureText(t, 20);
    DrawRectangle(0, H / 2 - 26, W, 52, Color{0, 0, 0, 170});
    DrawText(t, W / 2 - w / 2, H / 2 - 10, 20, Color{235, 220, 170, 255});
  }
}

void Game::DrawDevOverlay() {
  char buf[256];
  int y = 60;
  const int lh = 16;
  auto line = [&](const char* s) {
    DrawRectangle(8, y - 2, MeasureText(s, 14) + 8, lh, Color{0, 0, 0, 140});
    DrawText(s, 12, y, 14, Color{150, 230, 150, 230});
    y += lh;
  };

  if (devOverlay_) {
    snprintf(buf, sizeof(buf), "pos  %.0f, %.0f, %.0f   yaw %.0f  pitch %.0f",
             lp_.pos.x, lp_.pos.y, lp_.pos.z, lp_.yaw, lp_.viewPitch());
    line(buf);
    snprintf(buf, sizeof(buf), "speed %.2f  ground %d  crouch %.2f  hp %.0f",
             lp_.speed, (int)lp_.onGround, lp_.crouchT, lp_.health);
    line(buf);
    snprintf(buf, sizeof(buf), "particles %d   entities %d   fps %d",
             fx_.particleCount(), (int)client_.entities().size(), GetFPS());
    line(buf);
    // The clock reads as a 24 hour time so "time 0.46" stops meaning nothing,
    // and the countdown is to the next roll of the weather.
    const float hours = sky_.timeOfDay() * 24.0f;
    if (sky_.preset() == WeatherPreset::Cycle) {
      snprintf(buf, sizeof(buf),
               "weather %s (next in %ds)  storm %.2f  rain %.2f  %02d:%02d",
               sky_.bandName(), (int)sky_.secondsToNextRoll(), sky_.storm(),
               sky_.rain(), (int)hours, (int)((hours - (int)hours) * 60.0f));
    } else {
      snprintf(buf, sizeof(buf),
               "weather %s (pinned)  storm %.2f  rain %.2f  %02d:%02d",
               sky_.presetName(), sky_.storm(), sky_.rain(), (int)hours,
               (int)((hours - (int)hours) * 60.0f));
    }
    line(buf);
    snprintf(buf, sizeof(buf), "noclip %s   god %s   %s",
             devNoclip_ ? "ON" : "off", devGodMode_ ? "ON" : "off",
             singlePlayer_ ? "SINGLE PLAYER" : "NETWORKED");
    line(buf);
  }
  if (devHelp_) {
    y += 6;
    line("F1 this help    F3 stats     F5 noclip    F6 god mode");
    line("F7 weather      F8 hold: advance time     F9 test blast");
    line("F10 refill ammo                    Tab scoreboard");
  }
}

void Game::DrawMenu() {
  ClearBackground(Color{14, 16, 22, 255});
  const int W = GetScreenWidth(), H = GetScreenHeight();

  const char* title = "KAJ'S SHOOTER GAME 3D";
  DrawText(title, W / 2 - MeasureText(title, 46) / 2, (int)(H * 0.10f), 46,
           Color{240, 205, 90, 255});
  const char* sub = "built on the bones of Grand Theft Jack 3D";
  DrawText(sub, W / 2 - MeasureText(sub, 18) / 2, (int)(H * 0.10f) + 56, 18,
           Color{140, 150, 165, 255});

  if (mode_ == Mode::Error) {
    const char* e = errorText_.c_str();
    DrawText(e, W / 2 - MeasureText(e, 20) / 2, (int)(H * 0.4f), 20,
             Color{235, 90, 90, 255});
    const char* k = "press ENTER to go back";
    DrawText(k, W / 2 - MeasureText(k, 18) / 2, (int)(H * 0.4f) + 34, 18,
             Color{200, 200, 210, 255});
    if (IsKeyPressed(KEY_ENTER)) { mode_ = Mode::Menu; errorText_.clear(); }
    return;
  }

  // The map row is a menu entry rather than a hidden hotkey, because whoever
  // hosts picks the map for everyone -- joiners always play what the host
  // loaded, so it has to be obvious before you commit to hosting.
  int nMaps = 0;
  const char* const* maps = MapList(&nMaps);
  int curMap = 0;
  for (int i = 0; i < nMaps; ++i)
    if (opts_.map == maps[i]) curMap = i;

  char mapItem[96];
  snprintf(mapItem, sizeof(mapItem), "MAP:  %s", MapTitle(opts_.map.c_str()));
  char mapBlurb[96];
  snprintf(mapBlurb, sizeof(mapBlurb),
           "left/right to change   (%d of %d)", curMap + 1, nMaps);

  const int kMapRow = 4;
  const char* items[] = {"STORY MODE", "SINGLE PLAYER", "HOST A GAME",
                         "JOIN BY IP", mapItem, "QUIT"};
  const char* blurbs[] = {"waves of SWAT, then armour, then gunships",
                          "offline match against bots, dev tools on",
                          "host a match and play in it",
                          "connect to a friend's server", mapBlurb, ""};
  const int n = 6;
  if (!editingIp_) {
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) menuIndex_ = (menuIndex_ + 1) % n;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuIndex_ = (menuIndex_ + n - 1) % n;
  }

  const int baseY = (int)(H * 0.34f);
  for (int i = 0; i < n; ++i) {
    const bool sel = (i == menuIndex_);
    const int size = sel ? 30 : 26;
    const Color c = sel ? Color{255, 225, 120, 255} : Color{175, 180, 190, 255};
    const int w = MeasureText(items[i], size);
    DrawText(items[i], W / 2 - w / 2, baseY + i * 44, size, c);
    if (sel) {
      DrawText(">", W / 2 - w / 2 - 28, baseY + i * 44, size, c);
      if (blurbs[i][0]) {
        const int bw = MeasureText(blurbs[i], 15);
        DrawText(blurbs[i], W / 2 - bw / 2, baseY + i * 44 + size + 2, 15,
                 Color{120, 130, 145, 255});
      }
    }
  }

  const int ipY = baseY + n * 44 + 24;
  char ipLine[128];
  snprintf(ipLine, sizeof(ipLine), "server: %s:%u%s", ipBuffer_.c_str(),
           (unsigned)opts_.port, editingIp_ ? "_" : "");
  DrawText(ipLine, W / 2 - MeasureText(ipLine, 20) / 2, ipY, 20,
           editingIp_ ? Color{255, 235, 150, 255} : Color{150, 158, 170, 255});

  char nameLine[160];
  snprintf(nameLine, sizeof(nameLine), "name: %s     bots: %d",
           opts_.name.c_str(), opts_.bots);
  DrawText(nameLine, W / 2 - MeasureText(nameLine, 18) / 2, ipY + 26, 18,
           Color{150, 158, 170, 255});

  const char* help =
      "WASD move   Mouse look   LMB fire   RMB scope   Wheel/1-0 weapon   "
      "Space jump   Ctrl crouch   R reload   Tab scores   M music";
  DrawText(help, W / 2 - MeasureText(help, 15) / 2, H - 46, 15,
           Color{110, 118, 130, 255});
  const char* help2 =
      "E edit server address   +/- bots   Left/Right map   Enter select   "
      "F1 dev keys";
  DrawText(help2, W / 2 - MeasureText(help2, 15) / 2, H - 26, 15,
           Color{110, 118, 130, 255});

  if (editingIp_) {
    int ch;
    while ((ch = GetCharPressed()) > 0) {
      if (ipBuffer_.size() < 40 &&
          ((ch >= '0' && ch <= '9') || ch == '.' || (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') || ch == '-'))
        ipBuffer_.push_back((char)ch);
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !ipBuffer_.empty()) ipBuffer_.pop_back();
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) editingIp_ = false;
    return;
  }

  if (IsKeyPressed(KEY_E)) { editingIp_ = true; return; }
  if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
    opts_.bots = std::min(opts_.bots + 1, kMaxPlayers - 2);
  if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
    opts_.bots = std::max(opts_.bots - 1, 0);

  // Map selection. Reloading the world here means hosting picks it up
  // immediately; joiners always play whatever the host loaded. Left/right work
  // from anywhere in the menu, and Enter on the MAP row steps forward too.
  {
    int step = 0;
    if (IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressed(KEY_RIGHT)) step = 1;
    if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_LEFT)) step = -1;
    if (menuIndex_ == kMapRow &&
        (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)))
      step = 1;
    if (step != 0) {
      opts_.map = maps[((curMap + step) % nMaps + nMaps) % nMaps];
      world_.Load(DataPath("maps/" + opts_.map + ".map"));
      vehicles_.Reset(world_);
      // Decals are world-anchored, so anything left over from the last map
      // would hang in mid-air over the new one.
      fx_.Clear();
      drivingVehicle_ = -1;
      enterTimer_ = 0;
      return;   // do not also act on the Enter as a menu selection
    }
  }

  if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
    if (menuIndex_ == 0) StartStory();
    else if (menuIndex_ == 1) StartSinglePlayer();
    else if (menuIndex_ == 2) { opts_.host = true; StartHost(); }
    else if (menuIndex_ == 3) { opts_.host = false; StartJoin(); }
    else if (menuIndex_ == 5) quit_ = true;
  }
}

}  // namespace kaj
