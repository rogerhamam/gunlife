// Texture / sound / music loading. Everything here comes out of GTJ3D.gmk via
// tools/gmk_extract.py + tools/stage_assets.py.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "common.h"

namespace kaj {

// A GameMaker sprite: frames plus the origin point it was authored around.
struct SpriteSheet {
  std::vector<Texture2D> frames;
  Vector2 origin{0, 0};
  int width = 0, height = 0;

  bool valid() const { return !frames.empty(); }
  const Texture2D& frame(int i) const {
    const int n = static_cast<int>(frames.size());
    return frames[((i % n) + n) % n];
  }
};

// raylib will not play the same Sound twice concurrently, so each clip keeps a
// ring of aliases sharing one sample buffer. A bank may hold several clips --
// ricochets, fly-bys and impacts all pick randomly between recordings, the way
// Naval Command pools them.
struct SoundClip {
  Sound source{};
  std::vector<Sound> voices;
  int next = 0;
};

struct SoundBank {
  std::vector<SoundClip> clips;
  bool loaded = false;
};

// A vehicle body loaded from glTF, pre-fitted to game units.
struct VehicleModel {
  Model model{};
  bool loaded = false;
  float scale = 1.0f;     // model units -> world units
  float lift = 0.0f;      // world units to raise so the wheels sit on y = 0
  Vector3 centre{};       // model-space point to place at the chassis origin
  float yawFix = 0.0f;    // degrees, so the nose points along +X
  Vector3 size{};         // fitted world-space extents (length, height, width)
  Texture2D albedo{};     // base colour, unpacked from the GLB to PNG
  bool hasAlbedo = false;
};

class Assets {
 public:
  bool Load(const std::string& assetRoot);
  void Unload();

  const Texture2D& Tex(const std::string& name) const;
  const SpriteSheet& Sprite(const std::string& name) const;

  // Plays with the given volume/pan/pitch, cycling through the alias ring.
  void Play(const std::string& name, float volume = 1.0f, float pan = 0.5f,
            float pitch = 1.0f);
  // Starts a looping bed (wind, rain) and lets you ride its volume.
  void PlayLoop(const std::string& name, float volume);
  void SetLoopVolume(const std::string& name, float volume);
  void StopLoop(const std::string& name);
  bool Has(const std::string& name) const { return sounds_.count(name) > 0; }
  // Positional helper: attenuates by distance and pans by the listener's yaw.
  void PlayAt(const std::string& name, Vector3 pos, Vector3 listener,
              float listenerYaw, float range, float volume = 1.0f,
              float pitch = 1.0f);

  void StartMusic(int index);
  void StopMusic();
  void UpdateMusic();
  bool musicPlaying() const { return musicLoaded_; }
  int  musicIndex() const { return musicIndex_; }
  int  musicCount() const { return static_cast<int>(musicFiles_.size()); }

  const Texture2D& white() const { return white_; }

  // Vehicle bodies, loaded from assets/models/*.glb and fitted to GTJ3D's
  // units at load time: `scale` maps the model's own units onto ours from its
  // measured bounding box, `lift` drops its lowest point onto y = 0, and
  // `centre` recentres it on the chassis origin. Nothing here is hand-tuned to
  // a particular export -- swap the .glb and it still lands correctly.
  const VehicleModel& VehModel(int kind) const;
  // The tank's own geometry, cut into a hull and an independently traversing
  // turret by tools/split_tank.py. Both are fitted with the whole tank's
  // transform so they line up exactly when drawn together. `tankPivot` is the
  // turret ring in model units, relative to the fitted centre.
  const VehicleModel& tankHull() const { return tankHull_; }
  const VehicleModel& tankTurret() const { return tankTurret_; }
  Vector3 tankPivot() const { return tankPivot_; }
  bool tankSplit() const { return tankHull_.loaded && tankTurret_.loaded; }
  // Same treatment for the gunship: fuselage and a rotor that spins on it.
  const VehicleModel& heliBody() const { return heliBody_; }
  const VehicleModel& heliRotor() const { return heliRotor_; }
  Vector3 heliPivot() const { return heliPivot_; }
  bool heliSplit() const { return heliBody_.loaded && heliRotor_.loaded; }
  // Points every vehicle material at the world shader so the bodies pick up
  // the same fog, ambient and muzzle-flash lighting as the level geometry.
  void SetVehicleShader(Shader s);

  // GTJ3D's own obj_car skins. `interior` picks frame 1, the inside of the
  // shell, which is what it drew while you were sitting in the thing.
  // `colour` indexes kCarSkinNames; NearestCarSkin maps a map's paint colour
  // onto whichever of the seven GTJ3D actually shipped is closest.
  const Texture2D& CarSkin(int colour, bool interior) const;
  int NearestCarSkin(Color paint) const;
  bool haveCarSkins() const { return carSkinsLoaded_; }
  // spr_steering_wheel, three frames indexed by obj_player.turning.
  const SpriteSheet& steeringWheel() const { return steering_; }

 private:
  bool LoadTexture(const std::string& key, const std::string& path);
  bool LoadSprite(const std::string& key, const std::string& pattern, int frames,
                  Vector2 origin);
  bool LoadSoundBank(const std::string& key,
                     const std::vector<std::string>& files, int voices);

  std::string root_;
  std::map<std::string, Texture2D> textures_;
  std::map<std::string, SpriteSheet> sprites_;
  std::map<std::string, SoundBank> sounds_;
  std::map<std::string, Sound> loops_;

  std::vector<std::string> musicFiles_;
  Music music_{};
  bool musicLoaded_ = false;
  int musicIndex_ = -1;

  bool LoadVehicleModel(VehicleModel* out, const std::string& path,
                        float targetLength);
  // Loads a model using another one's fitted transform rather than measuring
  // its own bounding box -- needed for pieces of a split model, whose halves
  // would otherwise each be scaled to fill the same space.
  bool LoadFittedModel(VehicleModel* out, const std::string& path,
                       const VehicleModel& like);

  Texture2D white_{};
  Texture2D missing_{};
  SpriteSheet emptySprite_;
  VehicleModel vehModels_[5];   // saloon, van, tank, supercar, gunship
  VehicleModel tankHull_, tankTurret_;
  Vector3 tankPivot_{};
  VehicleModel heliBody_, heliRotor_;
  Vector3 heliPivot_{};

  // obj_car's shell skins: [colour][0 = outside, 1 = inside].
  static constexpr int kCarSkins = 7;
  Texture2D carSkin_[kCarSkins][2]{};
  bool carSkinsLoaded_ = false;
  SpriteSheet steering_;
};

}  // namespace kaj
