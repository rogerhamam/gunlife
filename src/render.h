// All drawing: fogged world geometry, players, entities, effects, the
// first-person viewmodel and the HUD.
//
// The viewmodel and HUD are laid out in GTJ3D's 640x480 ortho space and scaled
// uniformly to the window, so the guns sit exactly where they used to.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "assets.h"
#include "common.h"
#include "entities.h"
#include "net.h"
#include "player.h"
#include "world.h"

namespace kaj {

// Maps GTJ's 640x480 HUD space onto the real window.
struct HudTransform {
  float scale = 1.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;

  static HudTransform For(int screenW, int screenH);
  Vector2 P(float vx, float vy) const {
    return Vector2{offsetX + vx * scale, offsetY + vy * scale};
  }
  float S(float v) const { return v * scale; }
};

struct HudInfo {
  float health = kMaxHealth;
  float armor = 0.0f;
  int weapon = WEAPON_PISTOL;
  int mag = 0;
  int reserve = 0;
  bool reloading = false;
  bool dead = false;
  float respawnIn = 0.0f;
  float spreadDeg = 1.0f;
  float hitMarker = 0.0f;      // seconds remaining
  bool  hitWasHead = false;
  float damageFlash = 0.0f;
  float zoomT = 0.0f;
  int   fps = 0;
  float ping = 0.0f;
  bool  singlePlayer = false;
  std::string message;
  float messageTime = 0.0f;
  std::vector<std::string> killFeed;
  bool showScoreboard = false;
  // In a vehicle your carried weapon is stowed, so none of the gun UI --
  // name, ammo, reload banner or the number strip -- belongs on screen.
  bool inVehicle = false;
  // The hull you are sitting behind, 0..1, and what to call it. Drawn as its
  // own blue bar above your own health, because behind armour the number that
  // decides whether you are about to be on foot again is the vehicle's, not
  // yours.
  float vehicleHealth = 0.0f;
  const char* vehicleName = nullptr;
};

class Renderer {
 public:
  bool Init();
  void Shutdown();

  // Fog colour and range come from the weather, not the map, so geometry
  // always matches the sky behind it.
  // Transient point lights -- muzzle flashes, explosions. Cleared each frame;
  // push before BeginWorld. Only the four strongest are used.
  struct PointLight {
    Vector3 pos;
    float radius;
    Vector3 color;
  };
  void ClearLights() { lights_.clear(); }
  void AddLight(Vector3 pos, float radius, Vector3 color) {
    lights_.push_back(PointLight{pos, radius, color});
  }

  void BeginWorld(const Camera3D& cam, Color fogColor, float fogStart,
                  float fogEnd, Color ambient);
  void EndWorld();

  // DrawModel binds each material's own shader rather than whatever is active,
  // so glTF bodies have to be handed the world shader explicitly or they draw
  // unfogged and unlit while everything around them does not.
  bool fogReady() const { return fogReady_; }
  const Shader& worldShader() const { return fog_; }

  // The in-car view, per GTJ3D: the steering wheel drawn where the viewmodel
  // would be, its frame picked by which way you are turning (0 straight,
  // 1 left, 2 right), rolling with the front wheels.
  void DrawSteeringWheel(const Assets& assets, int turning, float wheelAngle,
                         float bump, const HudTransform& hud);

  void DrawWorld(const World& world, const Assets& assets, Vector3 camPos,
                 float viewDistance);
  // Draws the 3D bodies and queues their floating name tags; call
  // DrawNameTags() afterwards, in the 2D pass.
  void DrawPlayers(const Client& client, int selfId, const Assets& assets,
                   double renderTime, const Camera3D& cam);
  void DrawNameTags();
  // Kicks a player's weapon back when they fire; decays each frame.
  void PokeShotRecoil(int playerId) {
    if (playerId >= 0 && playerId < 32) shotRecoil_[playerId] = 1.0f;
  }
  void DecayShotRecoil(float dt) {
    for (float& r : shotRecoil_) r = (r > 0.0f) ? fmaxf(0.0f, r - dt * 7.0f) : 0.0f;
  }
  void DrawEntities(const std::vector<SimEntity>& ents, const Assets& assets,
                    const Camera3D& cam);

  // reloadT: 0 = not reloading, otherwise 0..1 progress through the reload.
  // shellReload weapons pulse it once per shell rather than once per magazine.
  // recoilT: 1 at the instant of firing, decaying to 0. Drives the procedural
  // kick for viewmodels whose sheets have no recoil of their own.
  void DrawViewmodel(const Assets& assets, int weapon, int animFrame,
                     float bobPhase, float bobAmount, float zoomT, bool dead,
                     float reloadT, float recoilT, const HudTransform& hud);
  void DrawHud(const Assets& assets, const HudInfo& info, const HudTransform& hud,
               int screenW, int screenH);
  void DrawScoreboard(const Client& client, int selfId, const HudTransform& hud);

 private:
  void DrawBrush(const Brush& b, const Assets& assets);

  struct NameTag {
    Vector2 screen;
    std::string name;
    float health;
    Color color;
  };
  std::vector<NameTag> tags_;
  float shotRecoil_[32] = {0};

  Shader fog_{};
  int locCamPos_ = -1, locFogColor_ = -1, locFogStart_ = -1, locFogEnd_ = -1;
  int locAmbient_ = -1;
  int locLightPosR_ = -1, locLightColor_ = -1, locLightCount_ = -1;
  int locFlatLit_ = -1;
  // Flat mode is on: textures read the same everywhere on the map, whatever
  // the distance or the weather. Set false to get distance fog and the
  // day/night ambient back.
  bool flatLit_ = true;
  std::vector<PointLight> lights_;
  bool fogReady_ = false;
};

// Shared helper: a yaw-rotated textured/solid box.
void DrawOrientedBox(Vector3 center, Vector3 size, float yawDeg, Color color);

}  // namespace kaj
