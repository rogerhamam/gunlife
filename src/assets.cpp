#include "assets.h"

#include <cstdio>

namespace kaj {
namespace {

// Sprite origins as authored in GTJ3D. Keeping them means the viewmodels line
// up on screen exactly the way they did in the original.
struct SpriteDef {
  const char* key;
  const char* file;
  int frames;
  float ox, oy;
};

const SpriteDef kSpriteDefs[] = {
    {"vm_knife",   "weapons/spr_fists_%d.png",           7, 128,   0},
    {"vm_hold",    "weapons/spr_fists_hold_%d.png",      7, 128,   0},
    {"vm_pistol",  "weapons/spr_handgun_%d.png",         5, 128,   0},
    {"vm_rifle",   "weapons/spr_assault_rifle_%d.png",   2, 145, -10},
    // Imported by tools/import_viewmodel.py; drawn at 1:1 with the origin set
    // so the sprite sits flush against the bottom of the 640x480 HUD space.
    {"vm_sniper",  "weapons/vm_sniper_%d.png",           1, 173,   4},
    {"vm_smg",     "weapons/vm_smg_%d.png",              1, 167,   4},
    {"vm_shotgun", "weapons/spr_shotgun_%d.png",         7, 160,  10},
    // Doom's super shotgun, split out of a strip by tools/import_sheet.py.
    // The eight frames are the whole break-open reload cycle. The origin is
    // chosen so that, at its 1.4 draw scale, the opaque pixels centre on the
    // same screen column as the other viewmodels and the stock lands on the
    // bottom edge of the 640x480 HUD space -- the sheet is authored well right
    // of centre, so it does not line up on its own.
    {"vm_ssg",     "weapons/vm_ssg_%d.png",              8, 220,  27},
    {"vm_rocket",  "weapons/spr_rocket_launcher_%d.png", 16, 153,  0},
    {"vm_grenade", "weapons/spr_grenade_%d.png",         3, 128,   0},
    // GTJ3D drew this in front of the driver's eye while you were in a car,
    // frame chosen by which way you were turning. Same three frames, drawn
    // where the viewmodel would otherwise be. 512x256, half scale.
    {"vm_wheel",   "weapons/spr_steering_wheel_%d.png",  3, 256,  12},

    // NOTE: the GTJ3D character sheets are no longer loaded. Enemies are a
    // procedurally built SWAT figure (see DrawSwatFigure in fx.cpp), which
    // reads the same from every angle instead of being a flat sprite.
    {"fx_blood",     "fx/spr_blood_%d.png",        3, 32, 32},
    {"fx_explosion", "fx/spr_explosion_%d.png",    1, 16, 16},
    {"fx_smoke",     "fx/spr_smoke_%d.png",        1, 16, 16},
    {"crosshair",    "fx/spr_crossheir_%d.png",    1, 16, 16},
    {"fx_crack",     "fx/spr_crack_%d.png",        3, 120, 120},
    {"proj_rocket",  "fx/spr_rocket_bullet_%d.png", 1, 32, 32},
    {"proj_grenade", "fx/tex_grenade_%d.png",      1, 16, 16},
    {"proj_bullet",  "fx/spr_mid_bullet_%d.png",   1, 32, 32},
};

const char* kTextureKeys[] = {
    "brick", "concrete", "wall_a", "wall_b", "wall_c", "wall_d", "floor",
    "metal", "wood", "crate", "crate2", "metal_panel", "metal_panel2",
    "stone", "stone2", "grate", "grate2", "door", "metal_door", "pillar",
    "bank", "shelves", "computer", "tech", "tech2", "tree", "sky",
    // Kingdom's, via tools/import_kingdom_trees.py: the trunk and canopy
    // skins for the map's `tree` command.
    "bark", "leaves",
};

// obj_car's `choose(tex_bluecar, tex_redcar, ...)` palette, in the order
// Assets::CarSkin indexes them, with the colour each skin actually is so a
// map's paint value can be matched to the nearest one GTJ3D shipped.
const char* const kCarSkinFiles[] = {
    "tex_bluecar", "tex_redcar", "tex_whitecar", "tex_yellowcar",
    "tex_pinkcar", "tex_limecar", "tex_blackcar",
};
const Color kCarSkinColors[] = {
    {40, 70, 170, 255}, {170, 40, 36, 255}, {226, 226, 230, 255},
    {228, 190, 40, 255}, {220, 110, 172, 255}, {124, 200, 60, 255},
    {34, 34, 38, 255},
};

struct SoundDef {
  const char* key;
  const char* files;   // one or more filenames, comma separated
  int voices;          // alias ring depth *per clip*
};

// Everything up to the divider is GTJ3D's original pool, untouched. Below it,
// Naval Command's library is layered in for the things GTJ3D never had:
// ricochets, near-misses, fly-bys, heavy explosions and weather.
const SoundDef kSoundDefs[] = {
    // The pistol is a Desert Eagle recording rather than GTJ3D's snd_handgun:
    // a hand cannon needs a report with some weight behind it, and GTJ's clip
    // is a light pop.
    {"pistol",      "deagle.wav",             6},
    {"shotgun",     "snd_shotgun.wav",        4},
    // Neither GTJ3D nor Naval Command has a double-barrel report, so these two
    // are synthesised from snd_shotgun / snd_reload_shotgun by
    // tools/make_ssg_sound.py -- same source recording, much bigger bore.
    {"supershotgun", "snd_supershotgun.wav",  4},
    {"ssg_break",   "snd_ssg_break.wav",      3},
    {"smg",         "snd_small.wav",          8},
    {"rifle",       "snd_large.wav",          8},
    {"rocket_fire", "snd_rocketlaunch.wav",   3},
    {"explosion",   "snd_explosion.wav",      5},
    {"explosion2",  "snd_explosion2.wav",     4},
    {"explosion3",  "snd_explosion3.wav",     4},
    {"jump",        "snd_jump.wav",           4},
    {"swing",       "snd_swing.wav",          4},
    {"punch",       "snd_punch.wav",          4},
    {"hurt",        "snd_hurt.wav",           4},
    {"hurt2",       "snd_hurt2.wav",          4},
    {"ow",          "snd_ow.wav",             4},
    {"pickup",      "snd_pickup.wav",         3},
    {"powerup",     "snd_powerup.wav",        3},
    {"reload",      "snd_reload_shotgun.wav", 4},
    {"car_door",    "snd_car_door.wav",       3},
    {"car_start",   "snd_car_start.wav",      2},
    {"crash",       "snd_crash.wav",          4},
    {"engine",      "snd_engine.wav",         2},
    {"heli",        "snd_helicopter1.wav",    2},
    {"heli_crash",  "snd_helicopter_crash.wav", 2},
    {"accel",       "snd_accel.wav",          2},
    // Spent cases hitting the ground, split out of a pack of recordings by
    // tools/split_casings.py. However many it finds is however many load.
    {"casing",      "casing_%d.wav",          3},
    {"tank_cannon", "nc_cannon_2.mp3",        3},
    {"tank_mg",     "nc_plane_fire.mp3",     10},
    {"beep",        "snd_lazer.wav",          6},
    {"beep2",       "snd_lazer2.wav",         6},
    {"tripwire",    "snd_electric.wav",       4},
    {"hum",         "snd_electric_hum.wav",   2},
    {"death",       "snd_human_scream.wav",   4},
    {"death2",      "snd_monster_die.wav",    4},
    {"bump",        "snd_bump.wav",           4},
    {"place",       "snd_door_shut.wav",      4},
    {"spawn",       "snd_teleport.wav",       3},
    {"alarm",       "snd_alarm_bell.wav",     2},
    {"good",        "snd_correct.wav",        2},
    {"bad",         "snd_wrong.wav",          2},
    {"headshot",    "snd_cash.wav",           4},
    {"hitmark",     "snd_bite.wav",           6},

    // ---- ballistic layers -------------------------------------------------
    // Three consolidated pools: one for rounds cracking past you, one for
    // rounds hitting the world (building, dirt, anything solid), one for
    // ricochets. Each mixes the dedicated bullet clips with Naval Command's.
    {"ricochet",    "bl_ricochet_1.mp3,bl_ricochet_2.mp3,bl_ricochet_3.mp3,"
                    "bl_ricochet_4.mp3,bl_ricochet_5.mp3,bl_ricochet_6.mp3",3},
    {"nearmiss",    "bl_passby_1.mp3,bl_passby_2.mp3,bl_passby_3.mp3,"
                    "nc_nearmiss_1.mp3,nc_nearmiss_2.mp3",                 4},
    {"passby",      "bl_passby_1.mp3,bl_passby_2.mp3,bl_passby_3.mp3,"
                    "nc_passby_0.mp3,nc_passby_1.mp3,nc_passby_2.mp3,"
                    "nc_passby_3.mp3,nc_passby_4.mp3",                     3},
    {"bullethit",   "bl_hitlayer_1.mp3,bl_hitlayer_2.mp3,bl_hitlayer_3.mp3,"
                    "nc_bullet_hit_1.mp3,nc_bullet_hit_2.mp3",             3},
    {"nearhit",     "bl_dirt_1.mp3,bl_dirt_2.mp3,bl_dirt_3.mp3,bl_dirt_4.mp3,"
                    "bl_hitlayer_1.mp3,bl_hitlayer_2.mp3,bl_hitlayer_3.mp3,"
                    "nc_impact_1.wav,nc_impact_2.wav",                     3},
    {"bigboom",     "nc_big_explosion.mp3",                                4},
    {"airburst",    "nc_aa_expl_1.mp3,nc_aa_expl_2.mp3,nc_aa_expl_3.mp3",  4},
    {"cannon",      "nc_cannon_1.mp3,nc_cannon_2.mp3,nc_cannon_3.mp3",     4},
    // The sniper's own report -- not the rifle pitched down.
    {"sniper",      "nc_cannon_1.mp3",                                     3},
    {"mg",          "nc_mg.mp3",                                           6},
    {"lastshot",    "nc_lastshot.mp3",                                     3},
    {"magclick",    "nc_reload.mp3",                                       3},
    {"distant",     "nc_distant_1.mp3,nc_distant_2.mp3",                   3},
    {"planefire",   "nc_plane_fire.mp3",                                   3},
    {"splash",      "nc_splash_1.mp3,nc_splash_2.mp3",                     3},
    {"thunder",     "nc_rain_thunder.mp3",                                 3},
    {"wind",        "nc_wind.wav",                                         1},
    // nc_plane_engine.mp3 is staged but not loaded: it is an aircraft bed with
    // nothing to attach to in an infantry shooter, and it is 3.4 MB of decode
    // on every launch.
};

const char* kMusicNames[] = {"bgm_theme1", "bgm_theme2", "bgm_theme3", "bgm_theme4"};

}  // namespace

bool Assets::LoadTexture(const std::string& key, const std::string& path) {
  if (!FileExists(path.c_str())) return false;
  Texture2D t = ::LoadTexture(path.c_str());
  if (t.id == 0) return false;
  // GTJ used texture_set_interpolation(false) -- keep the crunchy look, but
  // mipmap so distant walls do not shimmer.
  GenTextureMipmaps(&t);
  SetTextureFilter(t, TEXTURE_FILTER_POINT);
  SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
  textures_[key] = t;
  return true;
}

bool Assets::LoadSprite(const std::string& key, const std::string& pattern,
                        int frames, Vector2 origin) {
  SpriteSheet sheet;
  sheet.origin = origin;
  char buf[512];
  for (int i = 0; i < frames; ++i) {
    snprintf(buf, sizeof(buf), (root_ + "/" + pattern).c_str(), i);
    if (!FileExists(buf)) continue;
    Texture2D t = ::LoadTexture(buf);
    if (t.id == 0) continue;
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    sheet.frames.push_back(t);
    sheet.width = t.width;
    sheet.height = t.height;
  }
  if (sheet.frames.empty()) return false;
  sprites_[key] = sheet;
  return true;
}

bool Assets::LoadSoundBank(const std::string& key,
                           const std::vector<std::string>& files, int voices) {
  SoundBank bank;
  for (const std::string& f : files) {
    const std::string path = root_ + "/sounds/" + f;
    if (!FileExists(path.c_str())) continue;
    SoundClip clip;
    clip.source = LoadSound(path.c_str());
    if (clip.source.frameCount == 0) continue;
    clip.voices.push_back(clip.source);
    for (int i = 1; i < voices; ++i)
      clip.voices.push_back(LoadSoundAlias(clip.source));
    bank.clips.push_back(clip);
  }
  if (bank.clips.empty()) return false;
  bank.loaded = true;
  sounds_[key] = bank;
  return true;
}

bool Assets::Load(const std::string& assetRoot) {
  root_ = assetRoot;

  const double t0 = GetTime();

  Image w = GenImageColor(4, 4, WHITE);
  white_ = LoadTextureFromImage(w);
  UnloadImage(w);
  // Deleting a texture from assets/ is a supported thing to do, so the stand-in
  // is neutral grey concrete rather than a screaming magenta checker.
  Image m = GenImageColor(32, 32, Color{124, 122, 118, 255});
  ImageDrawRectangle(&m, 0, 0, 32, 1, Color{104, 102, 99, 255});
  ImageDrawRectangle(&m, 0, 0, 1, 32, Color{104, 102, 99, 255});
  missing_ = LoadTextureFromImage(m);
  SetTextureWrap(missing_, TEXTURE_WRAP_REPEAT);
  UnloadImage(m);

  int texCount = 0;
  for (const char* key : kTextureKeys) {
    if (LoadTexture(key, root_ + "/textures/" + key + ".png")) ++texCount;
  }
  const double tTex = GetTime();
  int sprCount = 0;
  for (const SpriteDef& d : kSpriteDefs) {
    if (LoadSprite(d.key, d.file, d.frames, Vector2{d.ox, d.oy})) ++sprCount;
  }
  const double tSpr = GetTime();
  int sndCount = 0, clipCount = 0;
  for (const SoundDef& d : kSoundDefs) {
    std::vector<std::string> files;
    std::string cur;
    for (const char* p = d.files;; ++p) {
      if (*p == ',' || *p == '\0') {
        if (!cur.empty()) files.push_back(cur);
        cur.clear();
        if (*p == '\0') break;
      } else {
        cur.push_back(*p);
      }
    }
    // A token containing %d is a numbered set: it expands over however many
    // files are actually on disk. tools/split_casings.py decides how many
    // casing impacts there are, so the bank cannot be a fixed list here.
    for (size_t i = 0; i < files.size();) {
      if (files[i].find("%d") == std::string::npos) { ++i; continue; }
      const std::string pattern = files[i];
      files.erase(files.begin() + i);
      char buf[256];
      for (int n = 0; n < 64; ++n) {
        snprintf(buf, sizeof(buf), pattern.c_str(), n);
        if (!FileExists((root_ + "/sounds/" + buf).c_str())) continue;
        files.insert(files.begin() + i, buf);
        ++i;
      }
    }
    if (LoadSoundBank(d.key, files, d.voices)) {
      ++sndCount;
      clipCount += static_cast<int>(sounds_[d.key].clips.size());
    } else {
      // Not an error: a bank whose clips have all been deleted from assets/
      // simply goes silent, and the game carries on without it.
      TraceLog(LOG_INFO, "ASSETS: sound bank '%s' has no clips, silenced", d.key);
    }
  }

  // Music lives in assets/music, and only there. It used to fall back to the
  // original GTJ3D download folder to avoid duplicating 130 MB of WAV, which
  // worked on the machine those files were downloaded to and nowhere else --
  // it was the one path in the game that reached outside its own directory,
  // so a copy of the project had no soundtrack. tools/localise_music.py
  // resamples the tracks to 22 kHz mono and puts them where they belong.
  for (const char* n : kMusicNames) {
    const std::string p = root_ + "/music/" + n + ".wav";
    if (FileExists(p.c_str())) musicFiles_.push_back(p);
  }

  // GTJ3D's obj_car shell skins, plus the steering wheel it drew in front of
  // the driver. Staged by tools/stage_assets.py into assets/vehicles.
  {
    int got = 0;
    for (int c = 0; c < kCarSkins; ++c) {
      for (int f = 0; f < 2; ++f) {
        const std::string p = root_ + "/vehicles/" + kCarSkinFiles[c] + "_" +
                              std::to_string(f) + ".png";
        if (!FileExists(p.c_str())) continue;
        carSkin_[c][f] = ::LoadTexture(p.c_str());
        if (carSkin_[c][f].id == 0) continue;
        // GTJ3D wrapped a 96x96 skin round the whole shell; without a wrap
        // the strips that run past the edge of the sheet clamp into a smear.
        SetTextureWrap(carSkin_[c][f], TEXTURE_WRAP_REPEAT);
        SetTextureFilter(carSkin_[c][f], TEXTURE_FILTER_BILINEAR);
        ++got;
      }
    }
    carSkinsLoaded_ = (got == kCarSkins * 2);
    if (!carSkinsLoaded_)
      TraceLog(LOG_WARNING,
               "ASSETS: %d/%d car skins found -- run tools/stage_assets.py; "
               "the driving view falls back to the glTF body", got,
               kCarSkins * 2);
  }
  LoadSprite("steering", "vehicles/spr_steering_wheel_%d.png", 3,
             Vector2{0, 0});
  {
    auto it = sprites_.find("steering");
    if (it != sprites_.end()) steering_ = it->second;
  }

  const double tSnd = GetTime();

  // Vehicle bodies. The saloon and the van share the Mercedes hull -- the van
  // is simply bigger and painted dark -- while the tank and the supercar have
  // their own. Lengths are in GTJ3D units, where its obj_car was 58 + a 25
  // nose; these bodies include their nose, so they read a little longer.
  LoadVehicleModel(&vehModels_[0], root_ + "/models/car.glb", 71.0f);
  // The SWAT van has its own body now -- a blacked-out truck rather than the
  // saloon scaled up. Falls back to the saloon hull if it is not staged.
  if (!LoadVehicleModel(&vehModels_[1], root_ + "/models/swat.glb", 86.0f))
    LoadVehicleModel(&vehModels_[1], root_ + "/models/car.glb", 82.0f);
  LoadVehicleModel(&vehModels_[2], root_ + "/models/tank.glb", 100.0f);
  LoadVehicleModel(&vehModels_[3], root_ + "/models/supercar.glb", 68.0f);
  // The tank's hull and turret, cut out of its own mesh so the turret can
  // traverse. Fitted with the whole tank's transform, so the two halves line
  // up seamlessly when drawn together.
  if (vehModels_[2].loaded) {
    LoadFittedModel(&tankHull_, root_ + "/models/tank_hull.obj", vehModels_[2]);
    LoadFittedModel(&tankTurret_, root_ + "/models/tank_turret.obj",
                    vehModels_[2]);
    // Printed by tools/split_tank.py when it cuts the mesh.
    tankPivot_ = Vector3{0.06066f, 0.0f, -0.00950f};
    if (tankHull_.loaded && tankTurret_.loaded)
      TraceLog(LOG_INFO, "ASSETS: tank split into hull + traversing turret");
  }
  LoadVehicleModel(&vehModels_[4], root_ + "/models/heli.glb", 150.0f);
  if (vehModels_[4].loaded) {
    LoadFittedModel(&heliBody_, root_ + "/models/heli_body.obj", vehModels_[4]);
    LoadFittedModel(&heliRotor_, root_ + "/models/heli_rotor.obj",
                    vehModels_[4]);
    // The mast, from tools/split_heli.py. Not split_tank.py: its deck finder
    // looks for where the cross-section narrows, and a helicopter's widest
    // slice is the rotor disc at the very top, so it cut the airframe in half
    // at the cabin and span everything above it.
    heliPivot_ = Vector3{-0.15169f, 0.0f, -0.00198f};
    if (heliBody_.loaded && heliRotor_.loaded)
      TraceLog(LOG_INFO, "ASSETS: gunship split into body + spinning rotor");
  }
  const double tMdl = GetTime();

  TraceLog(LOG_INFO,
           "ASSETS: %d textures, %d sprites, %d sound banks (%d clips), "
           "%d music tracks",
           texCount, sprCount, sndCount, clipCount, (int)musicFiles_.size());
  TraceLog(LOG_INFO, "ASSETS: load times  textures %.2fs  sprites %.2fs  "
                     "sounds %.2fs  models %.2fs  total %.2fs",
           tTex - t0, tSpr - tTex, tSnd - tSpr, tMdl - tSnd, tMdl - t0);
  return texCount > 0 && sprCount > 0;
}

// Fits a glTF body to GTJ3D's units from its own bounding box: no per-export
// magic numbers, so dropping in a different .glb still lands on the ground,
// centred, pointing down +X and the right length.
bool Assets::LoadVehicleModel(VehicleModel* out, const std::string& path,
                              float targetLength) {
  if (!FileExists(path.c_str())) {
    TraceLog(LOG_WARNING, "ASSETS: %s missing, that vehicle draws as boxes",
             path.c_str());
    return false;
  }
  out->model = LoadModel(path.c_str());
  if (out->model.meshCount == 0) {
    TraceLog(LOG_WARNING, "ASSETS: %s loaded no meshes", path.c_str());
    UnloadModel(out->model);
    return false;
  }
  const BoundingBox bb = GetModelBoundingBox(out->model);
  const Vector3 ext{bb.max.x - bb.min.x, bb.max.y - bb.min.y,
                    bb.max.z - bb.min.z};
  // Whichever horizontal axis is longer is the length; rotate so it lies on +X.
  const bool alongZ = ext.z > ext.x;
  const float rawLength = alongZ ? ext.z : ext.x;
  if (rawLength < 1e-4f) { UnloadModel(out->model); return false; }

  out->scale = targetLength / rawLength;
  // The longer horizontal axis is the length, but nothing in the file says
  // which end is the nose. These exports all model nose-down-negative, so
  // without the 180 the car drives backwards while you look forwards.
  out->yawFix = (alongZ ? 90.0f : 0.0f) + 180.0f;
  out->centre = Vector3{(bb.min.x + bb.max.x) * 0.5f, bb.min.y,
                        (bb.min.z + bb.max.z) * 0.5f};
  out->lift = 0.0f;   // centre.y already sits on the model's lowest point
  out->size = Vector3{targetLength, ext.y * out->scale,
                      (alongZ ? ext.x : ext.z) * out->scale};
  // raylib is built without JPEG support and these exports store their base
  // colour as JPEG inside the GLB, so the model would draw flat white. The
  // map is extracted to PNG alongside it by tools/extract_glb_textures.py.
  const std::string albedo =
      path.substr(0, path.size() - 4) + "_albedo.png";
  if (FileExists(albedo.c_str())) {
    // Qualified: Assets has its own LoadTexture(key, path) overload.
    Texture2D t = ::LoadTexture(albedo.c_str());
    if (t.id != 0) {
      GenTextureMipmaps(&t);
      SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
      for (int i = 0; i < out->model.materialCount; ++i)
        out->model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = t;
      out->albedo = t;
      out->hasAlbedo = true;
    }
  } else {
    TraceLog(LOG_WARNING,
             "ASSETS: %s has no _albedo.png -- run "
             "tools/extract_glb_textures.py or it draws untextured",
             GetFileName(path.c_str()));
  }

  out->loaded = true;
  TraceLog(LOG_INFO,
           "ASSETS: %s fitted -> %.0f x %.0f x %.0f units (scale %.4f, yaw %.0f)",
           GetFileName(path.c_str()), out->size.x, out->size.y, out->size.z,
           out->scale, out->yawFix);
  return true;
}

void Assets::SetVehicleShader(Shader s) {
  if (s.id == 0) return;
  for (VehicleModel* v : {&vehModels_[0], &vehModels_[1], &vehModels_[2],
                          &vehModels_[3], &vehModels_[4], &tankHull_,
                          &tankTurret_, &heliBody_, &heliRotor_}) {
    if (!v->loaded) continue;
    for (int i = 0; i < v->model.materialCount; ++i)
      v->model.materials[i].shader = s;
  }
}

bool Assets::LoadFittedModel(VehicleModel* out, const std::string& path,
                             const VehicleModel& like) {
  if (!FileExists(path.c_str())) {
    TraceLog(LOG_WARNING,
             "ASSETS: %s missing -- run tools/split_tank.py (tank) or "
             "tools/split_heli.py (gunship)", path.c_str());
    return false;
  }
  out->model = LoadModel(path.c_str());
  if (out->model.meshCount == 0) { UnloadModel(out->model); return false; }
  // Deliberately not measured: both halves must share the whole model's
  // scale, centre and yaw or they will not sit together.
  out->scale = like.scale;
  out->centre = like.centre;
  out->yawFix = like.yawFix;
  out->size = like.size;
  if (like.hasAlbedo) {
    for (int i = 0; i < out->model.materialCount; ++i)
      out->model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = like.albedo;
  }
  out->loaded = true;
  out->hasAlbedo = false;    // borrowed from `like`, not owned here
  return true;
}

const Texture2D& Assets::CarSkin(int colour, bool interior) const {
  if (colour < 0 || colour >= kCarSkins) colour = 0;
  const Texture2D& t = carSkin_[colour][interior ? 1 : 0];
  return t.id != 0 ? t : white_;
}

int Assets::NearestCarSkin(Color paint) const {
  // Plain squared distance in RGB. The seven skins are far enough apart in
  // colour that nothing cleverer earns its keep.
  int best = 0;
  long bestD = 1L << 30;
  for (int i = 0; i < kCarSkins; ++i) {
    const long dr = (long)paint.r - kCarSkinColors[i].r;
    const long dg = (long)paint.g - kCarSkinColors[i].g;
    const long db = (long)paint.b - kCarSkinColors[i].b;
    const long d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

const VehicleModel& Assets::VehModel(int kind) const {
  static const VehicleModel none;
  if (kind < 0 || kind > 4) return none;
  return vehModels_[kind];
}

void Assets::Unload() {
  for (VehicleModel* vp : {&tankHull_, &tankTurret_, &heliBody_, &heliRotor_}) {
    if (!vp->loaded) continue;
    // These borrow the tank's albedo rather than owning one; clear the
    // material's handle so UnloadModel does not free it out from under it.
    for (int i = 0; i < vp->model.materialCount; ++i)
      vp->model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    UnloadModel(vp->model);
    vp->loaded = false;
  }
  for (VehicleModel& v : vehModels_) {
    if (!v.loaded) continue;
    // The albedo is shared into the material, so drop it from the material
    // first or UnloadModel frees a texture we also own a handle to.
    for (int i = 0; i < v.model.materialCount; ++i)
      v.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    UnloadModel(v.model);
    if (v.hasAlbedo) { UnloadTexture(v.albedo); v.hasAlbedo = false; }
    v.loaded = false;
  }
  for (auto& row : carSkin_) {
    for (Texture2D& t : row) {
      if (t.id != 0) { UnloadTexture(t); t = Texture2D{}; }
    }
  }
  carSkinsLoaded_ = false;
  // steering_ is a copy of the sprites_ entry; the sprite map owns the frames.
  steering_ = SpriteSheet{};
  StopMusic();
  for (auto& kv : loops_) StopSound(kv.second);
  loops_.clear();
  for (auto& kv : sounds_) {
    for (SoundClip& c : kv.second.clips) {
      for (size_t i = 1; i < c.voices.size(); ++i) UnloadSoundAlias(c.voices[i]);
      UnloadSound(c.source);
    }
  }
  sounds_.clear();
  for (auto& kv : textures_) UnloadTexture(kv.second);
  textures_.clear();
  for (auto& kv : sprites_)
    for (Texture2D& t : kv.second.frames) UnloadTexture(t);
  sprites_.clear();
  UnloadTexture(white_);
  UnloadTexture(missing_);
}

const Texture2D& Assets::Tex(const std::string& name) const {
  auto it = textures_.find(name);
  return it == textures_.end() ? missing_ : it->second;
}

const SpriteSheet& Assets::Sprite(const std::string& name) const {
  auto it = sprites_.find(name);
  return it == sprites_.end() ? emptySprite_ : it->second;
}

void Assets::Play(const std::string& name, float volume, float pan, float pitch) {
  auto it = sounds_.find(name);
  if (it == sounds_.end() || volume <= 0.001f) return;
  SoundBank& b = it->second;
  SoundClip& c = b.clips[GetRandomValue(0, (int)b.clips.size() - 1)];
  Sound& s = c.voices[c.next];
  c.next = (c.next + 1) % static_cast<int>(c.voices.size());
  SetSoundVolume(s, Clampf(volume, 0.0f, 1.0f));
  SetSoundPan(s, Clampf(pan, 0.0f, 1.0f));
  SetSoundPitch(s, pitch);
  PlaySound(s);
}

void Assets::PlayLoop(const std::string& name, float volume) {
  if (loops_.count(name)) { SetLoopVolume(name, volume); return; }
  auto it = sounds_.find(name);
  if (it == sounds_.end()) return;
  // Take the bank's own source; raylib has no loop flag on Sound, so the
  // caller restarts it from Update when it runs out.
  Sound s = it->second.clips[0].source;
  SetSoundVolume(s, Clampf(volume, 0.0f, 1.0f));
  SetSoundPan(s, 0.5f);
  PlaySound(s);
  loops_[name] = s;
}

void Assets::SetLoopVolume(const std::string& name, float volume) {
  auto it = loops_.find(name);
  if (it == loops_.end()) return;
  SetSoundVolume(it->second, Clampf(volume, 0.0f, 1.0f));
  if (volume > 0.002f && !IsSoundPlaying(it->second)) PlaySound(it->second);
}

void Assets::StopLoop(const std::string& name) {
  auto it = loops_.find(name);
  if (it == loops_.end()) return;
  StopSound(it->second);
  loops_.erase(it);
}

void Assets::PlayAt(const std::string& name, Vector3 pos, Vector3 listener,
                    float listenerYaw, float range, float volume, float pitch) {
  const Vector3 d = Vector3Subtract(pos, listener);
  const float dist = Vector3Length(d);
  if (dist > range) return;
  const float atten = 1.0f - (dist / range);
  // GTJ pans nothing, but a stereo cue matters a lot in a shooter.
  const Vector3 right = FlatRight(listenerYaw);
  float side = dist > 0.001f ? Vector3DotProduct(Vector3Scale(d, 1.0f / dist), right) : 0.0f;
  // raylib pan: 0 = full right, 1 = full left.
  const float pan = Clampf(0.5f - side * 0.45f, 0.0f, 1.0f);
  Play(name, volume * atten * atten, pan, pitch);
}

void Assets::StartMusic(int index) {
  if (musicFiles_.empty()) return;
  StopMusic();
  musicIndex_ = ((index % (int)musicFiles_.size()) + (int)musicFiles_.size()) %
                (int)musicFiles_.size();
  music_ = LoadMusicStream(musicFiles_[musicIndex_].c_str());
  if (music_.stream.buffer == nullptr) return;
  music_.looping = true;
  musicLoaded_ = true;
  SetMusicVolume(music_, 0.35f);
  PlayMusicStream(music_);
}

void Assets::StopMusic() {
  if (!musicLoaded_) return;
  StopMusicStream(music_);
  UnloadMusicStream(music_);
  musicLoaded_ = false;
  musicIndex_ = -1;
}

void Assets::UpdateMusic() {
  if (musicLoaded_) UpdateMusicStream(music_);
}

}  // namespace kaj
