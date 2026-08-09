#include "fx.h"

#include <algorithm>
#include <cmath>

#include "rlgl.h"
#include "world.h"

namespace kaj {
namespace {

inline float Fr01() { return RandRange(0.0f, 1.0f); }

Vector3 RandUnitSphere() {
  for (int i = 0; i < 8; ++i) {
    const Vector3 u{Fr01() * 2 - 1, Fr01() * 2 - 1, Fr01() * 2 - 1};
    const float d = Vector3DotProduct(u, u);
    if (d <= 1.0f && d > 1e-4f) return Vector3Scale(u, 1.0f / sqrtf(d));
  }
  return Vector3{0, 1, 0};
}

// Soft radial blob with a little value noise so puffs are not perfect discs.
Texture2D MakePuffTexture(int size, float noiseAmount, float power) {
  Image img{};
  img.width = size;
  img.height = size;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  img.data = RL_MALLOC(static_cast<size_t>(size) * size * 4);
  unsigned char* d = static_cast<unsigned char*>(img.data);

  const float c = (size - 1) * 0.5f;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float dx = (x - c) / c, dy = (y - c) / c;
      float r = sqrtf(dx * dx + dy * dy);
      if (noiseAmount > 0.0f) {
        // Cheap hash noise breaks up the silhouette.
        const float h = sinf(x * 12.9898f + y * 78.233f) * 43758.5453f;
        r += (h - floorf(h) - 0.5f) * noiseAmount;
      }
      const float a = powf(1.0f - Clampf(r, 0.0f, 1.0f), power);
      const int o = (y * size + x) * 4;
      d[o] = d[o + 1] = d[o + 2] = 255;
      d[o + 3] = static_cast<unsigned char>(Clampf(a, 0.0f, 1.0f) * 255.0f);
    }
  }
  Texture2D t = LoadTextureFromImage(img);
  UnloadImage(img);
  SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
  return t;
}

// Fire palette: white-hot core -> orange -> deep red ember.
Color FireColor(float t, float alpha) {
  Vector3 c;
  if (t < 0.28f) {
    const float k = t / 0.28f;
    c = Vector3Lerp({1.00f, 0.97f, 0.82f}, {1.00f, 0.66f, 0.20f}, k);
  } else if (t < 0.7f) {
    const float k = (t - 0.28f) / 0.42f;
    c = Vector3Lerp({1.00f, 0.66f, 0.20f}, {0.72f, 0.20f, 0.05f}, k);
  } else {
    const float k = (t - 0.7f) / 0.3f;
    c = Vector3Lerp({0.72f, 0.20f, 0.05f}, {0.22f, 0.07f, 0.05f}, k);
  }
  return Color{(unsigned char)(c.x * 255), (unsigned char)(c.y * 255),
               (unsigned char)(c.z * 255),
               (unsigned char)(Clampf(alpha, 0, 1) * 255)};
}

// Smoke palette: warm grey turning cold and dark as it cools.
Color SmokeColor(float t, float alpha, Color base) {
  const float k = Clampf(t * 1.4f, 0.0f, 1.0f);
  const float r = base.r / 255.0f, g = base.g / 255.0f, b = base.b / 255.0f;
  const Vector3 c = Vector3Lerp({r, g, b}, {r * 0.42f, g * 0.44f, b * 0.5f}, k);
  return Color{(unsigned char)(c.x * 255), (unsigned char)(c.y * 255),
               (unsigned char)(c.z * 255),
               (unsigned char)(Clampf(alpha, 0, 1) * 255)};
}

}  // namespace

void DrawSkinnedBox(Vector3 c, Vector3 size, const Texture2D& tex, float uv0,
                    float uv1, Color tint) {
  const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
  // The sheet is a front view, so the sides only get a thin central strip --
  // enough to carry the right colours around the model without smearing.
  const float sideU0 = 0.42f, sideU1 = 0.58f;

  rlSetTexture(tex.id);
  rlBegin(RL_QUADS);

  // Per-face shading. Without it a uniform band of the sheet -- a policeman's
  // blue tunic, say -- covers the whole box in one flat colour and the figure
  // reads as a painted cube rather than a solid.
  auto shade = [&](Vector3 n) {
    float k = 0.68f;
    if (n.y > 0.5f) k = 1.0f;
    else if (n.y < -0.5f) k = 0.5f;
    else if (fabsf(n.x) > 0.5f) k = 0.80f;
    else if (n.z > 0.5f) k = 0.95f;   // face toward +Z stays brightest
    rlColor4ub((unsigned char)(tint.r * k), (unsigned char)(tint.g * k),
               (unsigned char)(tint.b * k), tint.a);
  };

  auto face = [&](Vector3 a, Vector3 b, Vector3 d, Vector3 e, float u0, float u1,
                  Vector3 n) {
    shade(n);
    rlNormal3f(n.x, n.y, n.z);
    rlTexCoord2f(u0, uv0); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(u0, uv1); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(u1, uv1); rlVertex3f(d.x, d.y, d.z);
    rlTexCoord2f(u1, uv0); rlVertex3f(e.x, e.y, e.z);
  };

  // front (+Z) and back (-Z), back mirrored so the figure reads correctly
  face({c.x - hx, c.y + hy, c.z + hz}, {c.x - hx, c.y - hy, c.z + hz},
       {c.x + hx, c.y - hy, c.z + hz}, {c.x + hx, c.y + hy, c.z + hz},
       0.0f, 1.0f, {0, 0, 1});
  face({c.x + hx, c.y + hy, c.z - hz}, {c.x + hx, c.y - hy, c.z - hz},
       {c.x - hx, c.y - hy, c.z - hz}, {c.x - hx, c.y + hy, c.z - hz},
       0.0f, 1.0f, {0, 0, -1});
  // sides
  face({c.x + hx, c.y + hy, c.z + hz}, {c.x + hx, c.y - hy, c.z + hz},
       {c.x + hx, c.y - hy, c.z - hz}, {c.x + hx, c.y + hy, c.z - hz},
       sideU0, sideU1, {1, 0, 0});
  face({c.x - hx, c.y + hy, c.z - hz}, {c.x - hx, c.y - hy, c.z - hz},
       {c.x - hx, c.y - hy, c.z + hz}, {c.x - hx, c.y + hy, c.z + hz},
       sideU0, sideU1, {-1, 0, 0});
  // top and bottom take the extreme rows of the band
  shade({0, 1, 0});
  rlNormal3f(0, 1, 0);
  rlTexCoord2f(sideU0, uv0); rlVertex3f(c.x - hx, c.y + hy, c.z - hz);
  rlTexCoord2f(sideU0, uv0); rlVertex3f(c.x - hx, c.y + hy, c.z + hz);
  rlTexCoord2f(sideU1, uv0); rlVertex3f(c.x + hx, c.y + hy, c.z + hz);
  rlTexCoord2f(sideU1, uv0); rlVertex3f(c.x + hx, c.y + hy, c.z - hz);
  shade({0, -1, 0});
  rlNormal3f(0, -1, 0);
  rlTexCoord2f(sideU0, uv1); rlVertex3f(c.x - hx, c.y - hy, c.z + hz);
  rlTexCoord2f(sideU0, uv1); rlVertex3f(c.x - hx, c.y - hy, c.z - hz);
  rlTexCoord2f(sideU1, uv1); rlVertex3f(c.x + hx, c.y - hy, c.z - hz);
  rlTexCoord2f(sideU1, uv1); rlVertex3f(c.x + hx, c.y - hy, c.z + hz);

  rlEnd();
  rlSetTexture(0);
}

// A solid box in the local frame, shaded per face so the figure has form.
static void SolidBox(Vector3 c, Vector3 size, Color col) {
  const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
  auto shade = [&](float k) {
    rlColor4ub((unsigned char)(col.r * k), (unsigned char)(col.g * k),
               (unsigned char)(col.b * k), col.a);
  };
  rlSetTexture(0);
  rlBegin(RL_QUADS);
  // +Z (front), -Z (back)
  shade(1.00f); rlNormal3f(0, 0, 1);
  rlVertex3f(c.x - hx, c.y + hy, c.z + hz); rlVertex3f(c.x - hx, c.y - hy, c.z + hz);
  rlVertex3f(c.x + hx, c.y - hy, c.z + hz); rlVertex3f(c.x + hx, c.y + hy, c.z + hz);
  shade(0.62f); rlNormal3f(0, 0, -1);
  rlVertex3f(c.x + hx, c.y + hy, c.z - hz); rlVertex3f(c.x + hx, c.y - hy, c.z - hz);
  rlVertex3f(c.x - hx, c.y - hy, c.z - hz); rlVertex3f(c.x - hx, c.y + hy, c.z - hz);
  // +X, -X
  shade(0.84f); rlNormal3f(1, 0, 0);
  rlVertex3f(c.x + hx, c.y + hy, c.z + hz); rlVertex3f(c.x + hx, c.y - hy, c.z + hz);
  rlVertex3f(c.x + hx, c.y - hy, c.z - hz); rlVertex3f(c.x + hx, c.y + hy, c.z - hz);
  shade(0.72f); rlNormal3f(-1, 0, 0);
  rlVertex3f(c.x - hx, c.y + hy, c.z - hz); rlVertex3f(c.x - hx, c.y - hy, c.z - hz);
  rlVertex3f(c.x - hx, c.y - hy, c.z + hz); rlVertex3f(c.x - hx, c.y + hy, c.z + hz);
  // top, bottom
  shade(1.12f); rlNormal3f(0, 1, 0);
  rlVertex3f(c.x - hx, c.y + hy, c.z - hz); rlVertex3f(c.x - hx, c.y + hy, c.z + hz);
  rlVertex3f(c.x + hx, c.y + hy, c.z + hz); rlVertex3f(c.x + hx, c.y + hy, c.z - hz);
  shade(0.48f); rlNormal3f(0, -1, 0);
  rlVertex3f(c.x - hx, c.y - hy, c.z + hz); rlVertex3f(c.x - hx, c.y - hy, c.z - hz);
  rlVertex3f(c.x + hx, c.y - hy, c.z - hz); rlVertex3f(c.x + hx, c.y - hy, c.z + hz);
  rlEnd();
}

// The enemy: a black SWAT operator. Built from boxes rather than a sprite, so
// it reads the same from any angle. Local +X is forward.
void DrawSwatFigure(float height, Color tint) {
  const float H = height;
  auto tinted = [&](Color c) {
    return Color{(unsigned char)(c.r * tint.r / 255),
                 (unsigned char)(c.g * tint.g / 255),
                 (unsigned char)(c.b * tint.b / 255), tint.a};
  };

  // A compact operator: narrow shoulders, tight limbs, everything pulled in
  // toward the centre line so the silhouette reads as a person rather than a
  // stack of crates.
  // legs -- trousers into boots
  SolidBox({0, H * 0.10f, -H * 0.09f}, {H * 0.13f, H * 0.20f, H * 0.12f},
           tinted(swat::kBoot));
  SolidBox({0, H * 0.10f,  H * 0.09f}, {H * 0.13f, H * 0.20f, H * 0.12f},
           tinted(swat::kBoot));
  SolidBox({0, H * 0.30f, -H * 0.09f}, {H * 0.14f, H * 0.22f, H * 0.13f},
           tinted(swat::kSuit));
  SolidBox({0, H * 0.30f,  H * 0.09f}, {H * 0.14f, H * 0.22f, H * 0.13f},
           tinted(swat::kSuit));
  // hips / belt rig
  SolidBox({0, H * 0.44f, 0}, {H * 0.24f, H * 0.09f, H * 0.30f},
           tinted(swat::kRig));
  // torso and plate carrier
  SolidBox({0, H * 0.61f, 0}, {H * 0.24f, H * 0.26f, H * 0.32f},
           tinted(swat::kSuit));
  SolidBox({H * 0.015f, H * 0.62f, 0}, {H * 0.24f, H * 0.20f, H * 0.34f},
           tinted(swat::kVest));
  // shoulder pads
  SolidBox({0, H * 0.735f, -H * 0.185f}, {H * 0.15f, H * 0.07f, H * 0.10f},
           tinted(swat::kVest));
  SolidBox({0, H * 0.735f,  H * 0.185f}, {H * 0.15f, H * 0.07f, H * 0.10f},
           tinted(swat::kVest));
  // upper arms, tucked against the ribs
  SolidBox({0, H * 0.635f, -H * 0.195f}, {H * 0.10f, H * 0.16f, H * 0.09f},
           tinted(swat::kSuit));
  SolidBox({0, H * 0.635f,  H * 0.195f}, {H * 0.10f, H * 0.16f, H * 0.09f},
           tinted(swat::kSuit));
  // forearms -- noticeably slimmer, angled in toward the weapon
  SolidBox({H * 0.045f, H * 0.525f, -H * 0.165f},
           {H * 0.075f, H * 0.13f, H * 0.070f}, tinted(swat::kSuit));
  SolidBox({H * 0.045f, H * 0.525f,  H * 0.165f},
           {H * 0.075f, H * 0.13f, H * 0.070f}, tinted(swat::kSuit));
  // neck
  SolidBox({0, H * 0.775f, 0}, {H * 0.10f, H * 0.05f, H * 0.11f},
           tinted(swat::kSuit));
  // head: balaclava with the face left open, then the helmet over the top
  SolidBox({0, H * 0.865f, 0}, {H * 0.155f, H * 0.13f, H * 0.165f},
           tinted(swat::kHelmet));
  // exposed face, set into the front of the head
  SolidBox({H * 0.072f, H * 0.858f, 0}, {H * 0.035f, H * 0.065f, H * 0.105f},
           tinted(swat::kSkin));
  // eyes
  SolidBox({H * 0.088f, H * 0.876f, -H * 0.029f},
           {H * 0.011f, H * 0.016f, H * 0.026f}, tinted(swat::kVisor));
  SolidBox({H * 0.088f, H * 0.876f,  H * 0.029f},
           {H * 0.011f, H * 0.016f, H * 0.026f}, tinted(swat::kVisor));
  // helmet shell and brim
  SolidBox({0, H * 0.938f, 0}, {H * 0.175f, H * 0.07f, H * 0.185f},
           tinted(swat::kHelmet));
  SolidBox({H * 0.072f, H * 0.920f, 0}, {H * 0.05f, H * 0.022f, H * 0.165f},
           tinted(swat::kHelmet));
}

// Bullet hole: black pit with a bright chipped rim, faded at the edge.
Texture2D MakeHoleTexture(int size) {
  Image img{};
  img.width = size;
  img.height = size;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  img.data = RL_MALLOC(static_cast<size_t>(size) * size * 4);
  unsigned char* d = static_cast<unsigned char*>(img.data);
  const float c = (size - 1) * 0.5f;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float dx = (x - c) / c, dy = (y - c) / c;
      const float h = sinf(x * 7.13f + y * 3.71f) * 43758.5453f;
      const float n = (h - floorf(h) - 0.5f) * 0.18f;
      const float r = Clampf(sqrtf(dx * dx + dy * dy) + n, 0.0f, 1.5f);
      const int o = (y * size + x) * 4;
      float a, v;
      if (r < 0.58f) {           // pit -- the dominant feature, near black
        a = 1.0f; v = 0.03f;
      } else if (r < 0.74f) {    // chipped rim
        const float k = (r - 0.58f) / 0.16f;
        a = 1.0f - k * 0.18f;
        v = 0.03f + k * 0.62f;
      } else {                   // dust halo
        const float k = Clampf((r - 0.74f) / 0.26f, 0.0f, 1.0f);
        a = (1.0f - k) * 0.5f;
        v = 0.65f - k * 0.3f;
      }
      const unsigned char cv = static_cast<unsigned char>(Clampf(v, 0, 1) * 255.0f);
      d[o] = d[o + 1] = d[o + 2] = cv;
      d[o + 3] = static_cast<unsigned char>(Clampf(a, 0, 1) * 255.0f);
    }
  }
  Texture2D t = LoadTextureFromImage(img);
  UnloadImage(img);
  SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
  return t;
}

bool FxSystem::Init() {
  puff_ = MakePuffTexture(64, 0.16f, 1.9f);
  spark_ = MakePuffTexture(32, 0.0f, 3.4f);
  hole_ = MakeHoleTexture(32);
  ready_ = (puff_.id != 0);
  parts_.reserve(kMaxParticles);
  return ready_;
}

void FxSystem::Shutdown() {
  if (!ready_) return;
  UnloadTexture(puff_);
  UnloadTexture(spark_);
  UnloadTexture(hole_);
  ready_ = false;
}

void FxSystem::Clear() {
  parts_.clear();
  debris_.clear();
  shrap_.clear();
  shocks_.clear();
  tracers_.clear();
  decals_.clear();
  bodyParts_.clear();
}

// ------------------------------------------------------------------ emitters

void FxSystem::EmitBurst(Vector3 pos, Vector3 dir, int count, float lifeAvg,
                         float sizeStart, float sizeEnd, float alphaPeak,
                         float tint, float buoy, float drag, Vector3 carryVel,
                         float posJitter) {
  const bool isFlash = tint > 0.5f;
  for (int i = 0; i < count; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(RandUnitSphere(),
                                         posJitter * sizeStart * Fr01()));
    p.vel = Vector3Add(Vector3Add(Vector3Scale(dir, 0.6f + Fr01() * 0.8f),
                                  Vector3Scale(RandUnitSphere(), sizeStart * 2.0f)),
                       carryVel);
    p.life = lifeAvg * (0.72f + Fr01() * 0.56f);
    p.seed = Fr01();
    p.startSize = sizeStart * (0.7f + Fr01() * 0.6f);
    p.endSize = sizeEnd * (0.75f + Fr01() * 0.5f);
    p.alphaPeak = alphaPeak;
    p.tint = tint;
    p.buoy = buoy;
    p.drag = drag;
    p.noTurb = isFlash;
    parts_.push_back(p);
  }
}

void FxSystem::EmitEruption(Vector3 c, float scale, int nFire, int nSmoke,
                            float baseSpeed) {
  for (int k = 0; k < nFire; ++k) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    const Vector3 dir = RandUnitSphere();
    Particle p;
    p.pos = Vector3Add(c, Vector3Scale(dir, scale * (0.2f + Fr01() * 0.4f)));
    p.vel = Vector3Scale(dir, baseSpeed * (0.5f + Fr01()));
    p.life = 0.30f + Fr01() * 0.45f;
    p.seed = Fr01();
    const float sz = scale * (0.18f + Fr01() * 0.30f);
    p.startSize = sz * 2.0f;
    p.endSize = sz * 4.5f;
    p.alphaPeak = 0.92f;
    p.tint = 1.0f;
    p.buoy = 0.0f;
    p.drag = 0.08f + Fr01() * 0.08f;
    p.noTurb = true;
    parts_.push_back(p);
  }
  for (int k = 0; k < nSmoke; ++k) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    const Vector3 dir = RandUnitSphere();
    Particle p;
    p.pos = Vector3Add(c, Vector3Scale(dir, scale * (0.2f + Fr01() * 0.4f)));
    p.vel = Vector3Scale(dir, baseSpeed * (0.3f + Fr01() * 0.5f));
    p.life = 1.4f + Fr01() * 2.2f;
    p.seed = Fr01();
    const float sz = scale * (0.22f + Fr01() * 0.45f);
    p.startSize = sz * 1.8f;
    p.endSize = sz * 5.5f;
    p.alphaPeak = 0.62f + Fr01() * 0.14f;
    p.tint = 0.0f;
    p.buoy = 2.5f * kUnitsPerMetre * 0.08f;
    p.drag = 0.14f + Fr01() * 0.14f;
    p.base = Color{120, 115, 108, 255};
    parts_.push_back(p);
  }
}

void FxSystem::EmitChaosLobes(Vector3 c, float scale, int n, float lifeBase) {
  for (int k = 0; k < n; ++k) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    const float a = Fr01() * 2.0f * PI;
    const float radius = scale * (0.30f + Fr01() * 1.30f);
    const Vector3 off{cosf(a) * radius, (Fr01() - 0.40f) * scale * 1.20f,
                      sinf(a) * radius};
    Particle p;
    p.pos = Vector3Add(c, off);
    p.vel = Vector3Add(Vector3Scale(off, 1.4f + Fr01() * 1.8f),
                       Vector3{0, scale * (0.6f + Fr01()), 0});
    p.life = lifeBase * (0.7f + Fr01() * 0.6f);
    p.seed = Fr01();
    const float sub = scale * (0.20f + Fr01() * 0.40f);
    p.startSize = sub * 2.0f;
    p.endSize = sub * 5.0f;
    p.alphaPeak = 0.85f + Fr01() * 0.10f;
    p.tint = 1.0f;
    p.buoy = 0.0f;
    p.drag = 0.10f;
    p.noTurb = true;
    parts_.push_back(p);
  }
}

void FxSystem::EmitBulbousCloud(Vector3 c, float scale, int n, float lifeBase) {
  // Blooms at near-full radius on frame one so the blast site is engulfed
  // immediately rather than growing into shape.
  const float s = scale * 1.35f;
  for (int k = 0; k < n; ++k) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    const Vector3 dir = RandUnitSphere();
    const float r = s * (0.15f + powf(Fr01(), 0.6f) * 0.95f);
    Particle p;
    p.pos = Vector3Add(c, Vector3{dir.x * r, dir.y * r * 0.7f, dir.z * r});
    p.vel = Vector3Add(Vector3Scale(dir, s * (0.25f + Fr01() * 0.5f)),
                       Vector3{0, s * 0.35f * Fr01(), 0});
    p.life = lifeBase * (0.6f + Fr01() * 0.8f);
    p.seed = Fr01();
    const float sz = s * (0.30f + Fr01() * 0.50f);
    p.startSize = sz * 1.4f;   // pre-bloomed
    p.endSize = sz * 3.2f;
    p.alphaPeak = 0.50f + Fr01() * 0.22f;
    p.tint = 0.0f;
    p.buoy = 6.0f;
    p.drag = 0.28f + Fr01() * 0.2f;
    p.base = Color{86, 82, 78, 255};
    parts_.push_back(p);
  }
}

void FxSystem::SpawnDebris(Vector3 pos, int count, float scaleMul,
                           float fireChance, float baseSpeed) {
  for (int k = 0; k < count; ++k) {
    if (debris_.size() > 220) break;
    DebrisChunk c;
    Vector3 dir = RandUnitSphere();
    dir.y = dir.y * 0.55f + 0.45f;
    dir = Vector3Normalize(dir);
    c.pos = Vector3Add(pos, Vector3Scale(dir, 2.0f));
    c.vel = Vector3Scale(dir, baseSpeed * (0.35f + sqrtf(Fr01()) * 1.1f));
    c.spin = Vector3{RandRange(-12, 12), RandRange(-12, 12), RandRange(-12, 12)};
    c.size = scaleMul * (0.6f + Fr01() * 1.6f);
    c.life = 3.5f + Fr01() * 3.5f;
    c.onFire = Fr01() < fireChance;
    const unsigned char g = static_cast<unsigned char>(28 + Fr01() * 22);
    c.color = Color{g, (unsigned char)(g - 3), (unsigned char)(g - 6), 255};
    debris_.push_back(c);
  }
}

void FxSystem::SpawnShrapnel(Vector3 pos, int count, float scale,
                             float baseSpeed) {
  for (int k = 0; k < count; ++k) {
    if (shrap_.size() > 260) break;
    Vector3 dir = RandUnitSphere();
    dir.y = dir.y * 0.7f + 0.25f;
    dir = Vector3Normalize(dir);
    Shrapnel s;
    s.pos = Vector3Add(pos, Vector3Scale(dir, 2.0f));
    s.prev = s.pos;
    s.vel = Vector3Scale(dir, baseSpeed * (0.6f + Fr01() * 0.8f));
    s.life = 0.45f + Fr01() * 0.6f;
    s.size = scale * (0.7f + Fr01() * 0.7f);
    shrap_.push_back(s);
  }
}

// The pressure wave is a ring of soft, fast-spreading dust rather than drawn
// geometry. The old version used a wireframe sphere and a segmented ring,
// which showed up as stray lines and facets cutting through the fireball.
void FxSystem::AddShockwave(Vector3 pos, float maxRadius) {
  const int n = 26;
  for (int i = 0; i < n; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    const float ang = (static_cast<float>(i) / n) * 2.0f * PI + Fr01() * 0.15f;
    const Vector3 out{cosf(ang), 0.0f, sinf(ang)};
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(out, maxRadius * 0.10f));
    p.vel = Vector3Add(Vector3Scale(out, maxRadius * (2.4f + Fr01() * 0.8f)),
                       Vector3{0, maxRadius * 0.25f * Fr01(), 0});
    p.life = 0.32f + Fr01() * 0.18f;
    p.seed = Fr01();
    p.startSize = maxRadius * 0.10f;
    p.endSize = maxRadius * 0.34f;
    p.alphaPeak = 0.30f;
    p.tint = 0.0f;
    p.buoy = 2.0f;
    p.drag = 0.04f;
    p.noTurb = true;
    p.base = Color{206, 202, 196, 255};
    parts_.push_back(p);
  }
}

void FxSystem::SpawnExplosion(Vector3 pos, float radius, bool nearGround) {
  const float R = fmaxf(radius, 20.0f);

  AddShockwave(pos, R * 1.45f);

  // Bright core -- short life, high alpha, no turbulence so it stays crisp.
  EmitBurst(pos, Vector3{0, R * 0.9f, 0}, 16, 0.26f, R * 0.30f, R * 0.85f,
            1.0f, 1.0f, 0.0f, 0.06f);

  EmitEruption(pos, R * 0.30f, 40, 26, R * 5.0f);
  EmitChaosLobes(pos, R * 0.32f, 12, 0.42f);

  // Bellowing smoke: a canopy above, a body at the seat of the blast, and a
  // low skirt that hugs the ground.
  EmitBulbousCloud(pos, R * 0.42f, 46, 3.6f);
  EmitBulbousCloud(Vector3Add(pos, Vector3{0, R * 0.55f, 0}), R * 0.34f, 26, 4.2f);
  if (nearGround) {
    EmitBulbousCloud(Vector3Add(pos, Vector3{0, R * 0.08f, 0}), R * 0.50f, 24, 3.0f);
    // Dust kicked off the street.
    EmitBurst(Vector3Add(pos, Vector3{0, 2.0f, 0}), Vector3{0, R * 0.5f, 0}, 20,
              2.4f, R * 0.16f, R * 0.75f, 0.42f, 0.0f, 3.0f, 0.3f);
    AddDecal(Vector3Add(pos, Vector3{0, 0.4f, 0}), Vector3{0, 1, 0}, R * 0.55f,
             Color{16, 13, 11, 200}, 26.0f);
  }

  SpawnShrapnel(pos, 22, R * 0.035f, R * 8.0f);
  SpawnDebris(pos, 12, R * 0.045f, 0.5f, R * 3.4f);
}

void FxSystem::MuzzleFlash(Vector3 pos, Vector3 dir, float scale,
                           float smokeMul) {
  // The painted flame is stripped out of the sprite sheets, so this is the
  // whole muzzle flash. The muzzle sits ~16 units from the eye, so a particle
  // here covers far more screen than the same particle out in the world --
  // sizes stay modest and lifetimes very short.
  const Vector3 side = Vector3Normalize(
      Vector3CrossProduct(dir, Vector3{0, 1, 0}));

  // Instant fire burst: a tight ball of flame that starts yellow-white and
  // drops through orange to red over a couple of frames. `tint = 1` runs the
  // fire palette, which is exactly that ramp, so a very short life gives the
  // whole yellow-to-red flash in one blink.
  const int cores = 3 + (smokeMul > 1.5f ? 3 : 0);
  for (int i = 0; i < cores; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(dir, scale * 0.25f * Fr01()));
    p.vel = Vector3Scale(dir, scale * (9.0f + Fr01() * 7.0f));
    p.life = 0.045f + Fr01() * 0.035f;
    p.seed = Fr01();
    p.startSize = scale * (0.40f + Fr01() * 0.26f);
    p.endSize = scale * 1.05f;
    p.alphaPeak = 0.82f;
    p.tint = 1.0f;
    p.drag = 0.02f;
    p.noTurb = true;
    parts_.push_back(p);
  }
  // Petals -- a few short-lived tongues fanning off the axis, which is what
  // makes it read as a burst rather than a blob.
  const int petals = 3 + static_cast<int>(smokeMul * 2.0f);
  for (int i = 0; i < petals; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    const float ang = Fr01() * 2.0f * PI;
    const Vector3 up = Vector3CrossProduct(side, dir);
    const Vector3 fan = Vector3Add(Vector3Scale(side, cosf(ang)),
                                   Vector3Scale(up, sinf(ang)));
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(dir, scale * 0.2f));
    p.vel = Vector3Add(Vector3Scale(dir, scale * (7.0f + Fr01() * 6.0f)),
                       Vector3Scale(fan, scale * (2.0f + Fr01() * 4.0f)));
    p.life = 0.03f + Fr01() * 0.045f;
    p.seed = Fr01();
    p.startSize = scale * (0.18f + Fr01() * 0.18f);
    p.endSize = scale * (0.5f + Fr01() * 0.4f);
    p.alphaPeak = 0.6f;
    p.tint = 1.0f;
    p.drag = 0.03f;
    p.noTurb = true;
    parts_.push_back(p);
  }
  // Barrel smoke. smokeMul lets the sniper and shotgun belch properly.
  const int n = static_cast<int>((2 + GetRandomValue(0, 2)) * smokeMul) + 1;
  for (int i = 0; i < n; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(RandUnitSphere(), scale * 0.25f));
    p.vel = Vector3Add(Vector3Scale(dir, scale * (2.0f + Fr01() * 3.0f) * smokeMul),
                       Vector3{RandRange(-1.5f, 1.5f), 2.0f + Fr01() * 2.5f,
                               RandRange(-1.5f, 1.5f)});
    p.life = (0.5f + Fr01() * 0.7f) * (0.7f + smokeMul * 0.45f);
    p.seed = Fr01();
    p.startSize = scale * (0.28f + Fr01() * 0.22f) * smokeMul;
    p.endSize = scale * (1.4f + Fr01() * 1.1f) * smokeMul;
    p.alphaPeak = (0.16f + Fr01() * 0.10f) * fminf(1.0f + smokeMul * 0.35f, 2.0f);
    p.tint = 0.0f;
    p.buoy = 5.0f;
    p.drag = 0.16f;
    p.base = Color{168, 165, 160, 255};
    parts_.push_back(p);
  }
}

void FxSystem::RocketTrail(Vector3 pos, Vector3 back, float dt) {
  const Vector3 b = Vector3LengthSqr(back) > 0.001f ? Vector3Normalize(back)
                                                    : Vector3{0, -1, 0};
  // Motor flame: bright, short-lived, hugging the nozzle.
  const int fire = 1 + (Fr01() < dt * 90.0f ? 1 : 0);
  for (int i = 0; i < fire; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(b, 4.0f + Fr01() * 3.0f));
    p.vel = Vector3Add(Vector3Scale(b, 40.0f + Fr01() * 50.0f),
                       Vector3Scale(RandUnitSphere(), 9.0f));
    p.life = 0.07f + Fr01() * 0.09f;
    p.seed = Fr01();
    p.startSize = 3.6f + Fr01() * 2.2f;
    p.endSize = 1.4f;
    p.alphaPeak = 0.85f;
    p.tint = 1.0f;
    p.drag = 0.06f;
    p.noTurb = true;
    parts_.push_back(p);
  }
  // Smoke column left hanging along the path.
  if (Fr01() < dt * 150.0f && static_cast<int>(parts_.size()) < kMaxParticles) {
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(b, 6.0f + Fr01() * 4.0f));
    p.vel = Vector3Add(Vector3Scale(b, 12.0f + Fr01() * 14.0f),
                       Vector3Scale(RandUnitSphere(), 4.0f));
    p.life = 1.6f + Fr01() * 2.4f;
    p.seed = Fr01();
    p.startSize = 3.0f + Fr01() * 2.0f;
    p.endSize = 20.0f + Fr01() * 16.0f;
    p.alphaPeak = 0.34f + Fr01() * 0.16f;
    p.tint = 0.0f;
    p.buoy = 5.0f;
    p.drag = 0.30f;
    p.base = Color{176, 172, 168, 255};
    parts_.push_back(p);
  }
}

void FxSystem::ImpactPuff(Vector3 pos, Vector3 normal, float scale, Color dust) {
  // Dust cone off the surface.
  for (int i = 0; i < 5; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    Vector3 d = Vector3Normalize(
        Vector3Add(normal, Vector3Scale(RandUnitSphere(), 0.7f)));
    p.pos = Vector3Add(pos, Vector3Scale(normal, scale * 0.2f));
    p.vel = Vector3Scale(d, scale * (5.0f + Fr01() * 9.0f));
    p.life = 0.35f + Fr01() * 0.5f;
    p.seed = Fr01();
    p.startSize = scale * (0.5f + Fr01() * 0.5f);
    p.endSize = scale * (2.0f + Fr01() * 1.6f);
    p.alphaPeak = 0.36f + Fr01() * 0.2f;
    p.tint = 0.0f;
    p.buoy = 2.0f;
    p.drag = 0.2f;
    p.base = dust;
    parts_.push_back(p);
  }
  // A couple of sparks.
  for (int i = 0; i < 3; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = pos;
    p.vel = Vector3Scale(
        Vector3Normalize(Vector3Add(normal, Vector3Scale(RandUnitSphere(), 1.1f))),
        scale * (18.0f + Fr01() * 22.0f));
    p.life = 0.10f + Fr01() * 0.14f;
    p.seed = Fr01();
    p.startSize = scale * 0.30f;
    p.endSize = scale * 0.10f;
    p.alphaPeak = 1.0f;
    p.tint = 1.0f;
    p.drag = 0.4f;
    p.noTurb = true;
    parts_.push_back(p);
  }
}

void FxSystem::BloodPuff(Vector3 pos, Vector3 dir, float scale) {
  // A fine dark spray rather than a few big red blobs: small droplets that
  // shrink as they fall, plus one soft mist puff to carry the colour.
  const int n = 9 + GetRandomValue(0, 5);
  for (int i = 0; i < n; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(RandUnitSphere(), scale * 0.25f));
    p.vel = Vector3Add(Vector3Scale(dir, scale * (2.0f + Fr01() * 5.0f)),
                       Vector3Scale(RandUnitSphere(), scale * 4.0f));
    p.life = 0.16f + Fr01() * 0.22f;
    p.seed = Fr01();
    p.startSize = scale * (0.18f + Fr01() * 0.22f);
    p.endSize = scale * (0.10f + Fr01() * 0.14f);
    p.alphaPeak = 0.85f;
    p.tint = 0.0f;
    p.buoy = -30.0f;
    p.drag = 0.4f;
    p.base = Color{96, 14, 12, 255};
    p.noTurb = true;
    parts_.push_back(p);
  }
  if (static_cast<int>(parts_.size()) < kMaxParticles) {
    Particle m;
    m.pos = pos;
    m.vel = Vector3Scale(dir, scale * 1.5f);
    m.life = 0.30f;
    m.seed = Fr01();
    m.startSize = scale * 0.5f;
    m.endSize = scale * 1.1f;
    m.alphaPeak = 0.34f;
    m.tint = 0.0f;
    m.buoy = -6.0f;
    m.drag = 0.25f;
    m.base = Color{112, 20, 18, 255};
    parts_.push_back(m);
  }
}

// Blood off a hit: a heavy exit cone along the bullet's path, a back-spatter
// cone thrown the other way (the round punching material back out of the entry
// wound), and a mist hanging over the whole thing.
void FxSystem::BloodSpray(Vector3 pos, Vector3 shotDir, float scale) {
  const Vector3 d = Vector3LengthSqr(shotDir) > 0.001f
                        ? Vector3Normalize(shotDir)
                        : Vector3{0, 1, 0};
  const Vector3 back = Vector3Scale(d, -1.0f);

  // Exit cone: fast, tight, thrown along the bullet's path.
  const int n = 26 + GetRandomValue(0, 14);
  for (int i = 0; i < n; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(d, scale * 0.3f));
    p.vel = Vector3Add(Vector3Scale(d, scale * (7.0f + Fr01() * 20.0f)),
                       Vector3Scale(RandUnitSphere(), scale * 5.0f));
    p.life = 0.18f + Fr01() * 0.34f;
    p.seed = Fr01();
    p.startSize = scale * (0.14f + Fr01() * 0.22f);
    p.endSize = scale * (0.08f + Fr01() * 0.12f);
    p.alphaPeak = 0.92f;
    p.tint = 0.0f;
    p.buoy = -34.0f;
    p.drag = 0.45f;
    p.base = Color{104, 12, 10, 255};
    p.noTurb = true;
    parts_.push_back(p);
  }

  // Back-spatter: a narrower, slower cone driven out of the entry wound toward
  // whoever fired. Reads as the round shoving material back out.
  const int nb = 12 + GetRandomValue(0, 8);
  for (int i = 0; i < nb; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(back, scale * 0.22f));
    p.vel = Vector3Add(Vector3Scale(back, scale * (4.0f + Fr01() * 11.0f)),
                       Vector3Scale(RandUnitSphere(), scale * 3.2f));
    p.life = 0.16f + Fr01() * 0.26f;
    p.seed = Fr01();
    p.startSize = scale * (0.11f + Fr01() * 0.16f);
    p.endSize = scale * (0.06f + Fr01() * 0.10f);
    p.alphaPeak = 0.88f;
    p.tint = 0.0f;
    p.buoy = -30.0f;
    p.drag = 0.5f;
    p.base = Color{116, 16, 13, 255};
    p.noTurb = true;
    parts_.push_back(p);
  }

  // Mist hanging in the air at the wound.
  for (int i = 0; i < 7; ++i) {
    if (static_cast<int>(parts_.size()) >= kMaxParticles) break;
    Particle p;
    p.pos = Vector3Add(pos, Vector3Scale(RandUnitSphere(), scale * 0.45f));
    p.vel = Vector3Add(Vector3Scale(RandUnitSphere(), scale * 2.4f),
                       Vector3Scale(d, scale * 1.5f));
    p.life = 0.32f + Fr01() * 0.30f;
    p.seed = Fr01();
    p.startSize = scale * 0.55f;
    p.endSize = scale * 1.6f;
    p.alphaPeak = 0.36f;
    p.tint = 0.0f;
    p.buoy = -5.0f;
    p.drag = 0.3f;
    p.base = Color{122, 22, 20, 255};
    parts_.push_back(p);
  }
}

void FxSystem::AddTracer(Vector3 a, Vector3 b, Color c, float width,
                         float speed) {
  if (tracers_.size() > 220) tracers_.erase(tracers_.begin());
  Tracer t;
  t.a = a;
  t.b = b;
  t.color = c;
  t.width = width;
  t.speed = speed > 1.0f ? speed : 9000.0f;
  Vector3 d = Vector3Subtract(b, a);
  t.total = Vector3Length(d);
  t.dir = t.total > 0.001f ? Vector3Scale(d, 1.0f / t.total) : Vector3{0, 0, 1};
  t.streak = fminf(fmaxf(t.total * 0.25f, 40.0f), 160.0f);
  // Live long enough to cross the whole path, then a beat to fade.
  t.maxLife = t.total / t.speed + 0.05f;
  tracers_.push_back(t);
}

void FxSystem::EjectCasing(Vector3 pos, Vector3 shotDir, float scale) {
  // Cases pile up fast on a gun doing 1200 rpm, so they ride the same budget
  // as blast debris and the oldest are dropped once it is full.
  if (debris_.size() > 320) debris_.erase(debris_.begin());
  DebrisChunk c;
  // Out of the right-hand side of the breech and a little up, as a real
  // ejector throws them.
  Vector3 right = Vector3CrossProduct(shotDir, Vector3{0, 1, 0});
  if (Vector3LengthSqr(right) < 1e-5f) right = Vector3{1, 0, 0};
  right = Vector3Normalize(right);
  c.pos = Vector3Add(pos, Vector3Scale(right, 2.0f * scale));
  // Thrown up and out rather than flat sideways, and with almost no drag, so
  // it climbs, tips over and drops back on a clean parabola instead of being
  // damped into a dead sideways lob.
  c.vel = Vector3Add(Vector3Scale(right, 20.0f + Fr01() * 16.0f),
                     Vector3{RandRange(-6.0f, 6.0f), 62.0f + Fr01() * 26.0f,
                             RandRange(-6.0f, 6.0f)});
  c.spin = Vector3{RandRange(-900.0f, 900.0f), RandRange(-900.0f, 900.0f),
                   RandRange(-900.0f, 900.0f)};
  c.size = 0.9f * scale;
  c.life = 9.0f + Fr01() * 4.0f;
  c.onFire = false;
  c.drag = 0.97f;
  c.casing = true;
  c.color = Color{198, 156, 62, 255};        // brass
  debris_.push_back(c);
}

void FxSystem::ShellTracer(Vector3 a, Vector3 b, Vector3 dir) {
  // Fat, bright yellow, and slow enough that you can follow it out to the
  // target instead of it arriving instantly like a rifle round.
  AddTracer(a, b, Color{255, 240, 120, 255}, 7.0f, 2400.0f);
  AddTracer(a, b, Color{255, 186, 44, 220}, 12.5f, 2400.0f);

  // Smoke laid along the flight path. Spaced by distance rather than by count
  // so a long shot and a short one leave the same density of trail.
  const float total = Vector3Distance(a, b);
  const int puffs = (int)fminf(60.0f, total / 26.0f);
  for (int i = 0; i < puffs; ++i) {
    const float t = (i + 0.5f) / (float)puffs;
    const Vector3 p = Vector3Lerp(a, b, t);
    Particle s;
    s.pos = Vector3{p.x + RandRange(-3.0f, 3.0f), p.y + RandRange(-3.0f, 3.0f),
                    p.z + RandRange(-3.0f, 3.0f)};
    s.vel = Vector3{RandRange(-4.0f, 4.0f), 3.0f + Fr01() * 5.0f,
                    RandRange(-4.0f, 4.0f)};
    // The trail thins out toward the target, the way a real one does as the
    // propellant gas is left further behind.
    s.life = 2.2f + Fr01() * 2.0f - t * 0.9f;
    s.age = 0.0f;
    s.seed = Fr01() * 100.0f;
    s.startSize = 7.0f + t * 5.0f;
    s.endSize = 34.0f + t * 22.0f;
    s.alphaPeak = 0.42f * (1.0f - t * 0.45f);
    s.tint = i < 3 ? 1.0f : 0.0f;     // still burning right at the muzzle
    s.buoy = 5.0f;
    s.drag = 0.55f;
    if ((int)parts_.size() < kMaxParticles) parts_.push_back(s);
  }
  (void)dir;
}

void FxSystem::AddDecal(Vector3 p, Vector3 n, float size, Color c, float life) {
  if (decals_.size() > 420) decals_.erase(decals_.begin());
  Decal d;
  d.pos = Vector3Add(p, Vector3Scale(n, 0.4f));
  d.normal = n;
  Vector3 u = fabsf(n.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
  d.right = Vector3Normalize(Vector3CrossProduct(n, u));
  d.size = size;
  d.halfX = size;
  d.halfY = size;
  d.color = c;
  d.maxLife = life;
  decals_.push_back(d);
}

// A lasting mark where a round struck. These outlive everything else so a
// firefight leaves the wall visibly chewed up.
void FxSystem::AddBulletHole(Vector3 p, Vector3 n, Vector3 faceMin,
                             Vector3 faceMax) {
  if (decals_.size() > 420) decals_.erase(decals_.begin());
  Decal d;
  // Stand the mark well clear of the face it belongs to. At 0.35 units the
  // quad was inside the depth-test tolerance of the wall and only the corners
  // -- where two faces meet and the offset doubles up -- ever showed.
  d.pos = Vector3Add(p, Vector3Scale(n, 1.2f));
  d.normal = n;
  Vector3 u = fabsf(n.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
  d.right = Vector3Normalize(Vector3CrossProduct(n, u));
  // Random roll so repeated hits do not stamp an identical sprite.
  const float rot = Fr01() * 2.0f * PI;
  const Vector3 up = Vector3CrossProduct(n, d.right);
  d.right = Vector3Add(Vector3Scale(d.right, cosf(rot)),
                       Vector3Scale(up, sinf(rot)));
  d.size = 1.7f + Fr01() * 0.9f;
  d.halfX = d.size;
  d.halfY = d.size;

  // Trim the mark to the face it landed on. A hole punched near an edge used
  // to hang half of itself out past the corner, floating in mid-air; now the
  // overhanging side is cut back to the edge instead. Each axis is measured
  // separately, so a hole beside a vertical edge only narrows horizontally.
  if (faceMax.x > faceMin.x || faceMax.y > faceMin.y || faceMax.z > faceMin.z) {
    const Vector3 up = Vector3CrossProduct(n, d.right);
    auto reach = [&](Vector3 axis) {
      // How far the quad can extend along `axis` before leaving the box, in
      // both directions; the smaller of the two is what bounds the half-size.
      float lim = 1e9f;
      const float o[3] = {p.x, p.y, p.z};
      const float a[3] = {axis.x, axis.y, axis.z};
      const float mn[3] = {faceMin.x, faceMin.y, faceMin.z};
      const float mx[3] = {faceMax.x, faceMax.y, faceMax.z};
      for (int i = 0; i < 3; ++i) {
        if (fabsf(a[i]) < 1e-4f) continue;
        lim = fminf(lim, fmaxf(0.0f, (a[i] > 0.0f ? mx[i] - o[i] : o[i] - mn[i])
                                         / fabsf(a[i])));
        lim = fminf(lim, fmaxf(0.0f, (a[i] > 0.0f ? o[i] - mn[i] : mx[i] - o[i])
                                         / fabsf(a[i])));
      }
      return lim;
    };
    d.halfX = fminf(d.halfX, reach(d.right));
    d.halfY = fminf(d.halfY, reach(up));
    // Right on an edge there is no room at all; drop it rather than draw a
    // sliver.
    if (d.halfX < 0.35f || d.halfY < 0.35f) return;
  }

  d.color = Color{255, 255, 255, 255};
  d.maxLife = 90.0f;
  d.hole = true;
  decals_.push_back(d);
}

// ------------------------------------------------------------------- corpses

void FxSystem::SpawnCorpse(Vector3 feet, float yaw, float height,
                           Color teamColor, const Texture2D* skin,
                           Vector3 hitPos, Vector3 shotDir, float force,
                           bool headshot, bool gib) {
  const Vector3 dir = Vector3LengthSqr(shotDir) > 0.001f
                          ? Vector3Normalize(shotDir)
                          : Vector3{0, 0, 1};
  const Vector3 fwd = FlatForward(yaw);
  const Vector3 right = FlatRight(yaw);

  // The SWAT figure broken into its constituent pieces. Fourteen segments
  // rather than seven, so the body comes apart at real joints.
  struct Piece {
    float cy, ox;           // centre height, lateral offset (height fractions)
    float sx, sy, sz;       // size (height fractions)
    Color col;
    float u0, u1;           // sheet band, if a skin is supplied
  };
  const Piece kPieces[] = {
      {0.955f,  0.00f, 0.21f, 0.08f, 0.22f, swat::kHelmet, 0.00f, 0.10f},  // helmet
      {0.875f,  0.00f, 0.19f, 0.15f, 0.20f, swat::kHelmet, 0.06f, 0.22f},  // head
      {0.790f,  0.00f, 0.12f, 0.05f, 0.14f, swat::kSuit,   0.20f, 0.26f},  // neck
      {0.700f, -0.27f, 0.14f, 0.13f, 0.13f, swat::kVest,   0.24f, 0.36f},  // L upper arm
      {0.700f,  0.27f, 0.14f, 0.13f, 0.13f, swat::kVest,   0.24f, 0.36f},  // R upper arm
      {0.570f, -0.27f, 0.13f, 0.14f, 0.12f, swat::kSuit,   0.36f, 0.50f},  // L forearm
      {0.570f,  0.27f, 0.13f, 0.14f, 0.12f, swat::kSuit,   0.36f, 0.50f},  // R forearm
      {0.690f,  0.00f, 0.30f, 0.14f, 0.46f, swat::kVest,   0.24f, 0.40f},  // chest
      {0.570f,  0.00f, 0.29f, 0.12f, 0.43f, swat::kSuit,   0.40f, 0.54f},  // abdomen
      {0.460f,  0.00f, 0.30f, 0.10f, 0.42f, swat::kRig,    0.54f, 0.62f},  // pelvis
      {0.320f, -0.13f, 0.17f, 0.24f, 0.17f, swat::kSuit,   0.62f, 0.80f},  // L thigh
      {0.320f,  0.13f, 0.17f, 0.24f, 0.17f, swat::kSuit,   0.62f, 0.80f},  // R thigh
      {0.110f, -0.13f, 0.15f, 0.22f, 0.15f, swat::kBoot,   0.80f, 1.00f},  // L shin
      {0.110f,  0.13f, 0.15f, 0.22f, 0.15f, swat::kBoot,   0.80f, 1.00f},  // R shin
  };
  const int nPieces = (int)(sizeof(kPieces) / sizeof(kPieces[0]));

  for (int i = 0; i < nPieces; ++i) {
    if (bodyParts_.size() > 300) break;
    const Piece& q = kPieces[i];
    const Vector3 centre =
        Vector3Add(Vector3Add(feet, Vector3{0, height * q.cy, 0}),
                   Vector3Scale(right, height * q.ox));

    // How badly this piece was hit. Anything inside a hand's width of the
    // wound channel is destroyed outright and shatters; further out the piece
    // survives but is thrown, and the far end of the body barely moves.
    const float dw = Vector3Distance(centre, hitPos);
    const float near = height * 0.16f;
    const float mid = height * 0.42f;
    const bool shatters = gib || dw < near;
    const float prox = Clampf(1.0f - (dw - near) / (mid - near), 0.0f, 1.0f);

    // Pieces close to the wound break into fragments; the count scales with
    // how central the hit was, so a chest shot scatters more than a foot shot.
    const int frags = shatters
                          ? (gib ? 3 + GetRandomValue(0, 3)
                                 : 2 + (int)(prox * 3.0f) + GetRandomValue(0, 1))
                          : 1;

    for (int f = 0; f < frags; ++f) {
      if (bodyParts_.size() > 300) break;
      BodyPart b;
      const float shrink = (frags > 1) ? powf(1.0f / frags, 0.34f) : 1.0f;
      b.size = Vector3{height * q.sx * shrink * (0.7f + Fr01() * 0.5f),
                       height * q.sy * shrink * (0.7f + Fr01() * 0.5f),
                       height * q.sz * shrink * (0.7f + Fr01() * 0.5f)};
      b.pos = (frags > 1)
                  ? Vector3Add(centre, Vector3Scale(RandUnitSphere(),
                                                    height * q.sy * 0.5f))
                  : centre;
      b.color = q.col;
      // Fragments show meat on their broken faces.
      if (frags > 1 && f > 0) {
        b.color = Color{(unsigned char)(88 + GetRandomValue(0, 40)),
                        (unsigned char)(16 + GetRandomValue(0, 14)),
                        (unsigned char)(14 + GetRandomValue(0, 12)), 255};
      }
      b.skin = (frags == 1) ? skin : nullptr;
      b.uv0 = q.u0;
      b.uv1 = q.u1;
      b.bleeds = true;
      b.life = gib ? 7.0f + Fr01() * 3.0f : 10.0f + Fr01() * 5.0f;

      float amount = force * (0.22f + prox * 1.30f);
      if (headshot && i <= 1) amount *= 2.2f;
      if (frags > 1) amount *= 1.25f + Fr01() * 0.9f;

      if (shatters) {
        // Blown off the body: away from the wound, with the shot's push added.
        Vector3 out = Vector3Subtract(b.pos, hitPos);
        if (Vector3LengthSqr(out) < 0.01f) out = RandUnitSphere();
        out = Vector3Normalize(
            Vector3Add(Vector3Normalize(out), Vector3Scale(RandUnitSphere(), 0.55f)));
        b.vel = Vector3Add(Vector3Scale(out, amount * (0.9f + Fr01() * 1.1f)),
                           Vector3Scale(dir, amount * 0.55f));
        b.vel.y += amount * (0.35f + Fr01() * 0.6f);
        b.spin = Vector3{RandRange(-30, 30), RandRange(-30, 30), RandRange(-30, 30)};
      } else {
        // Still attached in spirit: carried along by the body's collapse.
        b.vel = Vector3Add(Vector3Scale(dir, amount),
                           Vector3Scale(RandUnitSphere(), amount * 0.28f));
        b.vel.y += amount * 0.30f;
        b.spin = Vector3{RandRange(-9, 9), RandRange(-9, 9), RandRange(-9, 9)};
      }
      bodyParts_.push_back(b);

      // Each shattered piece throws its own gout of blood.
      if (frags > 1 && f == 0) BloodSpray(b.pos, dir, height * 0.35f);
    }
  }

  // Blood off the wound, thrown along the round.
  BloodSpray(hitPos, dir, height * 0.5f);

  if (gib) {
    // Viscera: many small wet chunks in every direction, plus a heavy mist.
    const int chunks = 26 + GetRandomValue(0, 14);
    for (int i = 0; i < chunks; ++i) {
      if (bodyParts_.size() > 200) break;
      BodyPart b;
      const Vector3 out = RandUnitSphere();
      b.pos = Vector3Add(hitPos, Vector3Scale(out, height * 0.15f));
      b.vel = Vector3Scale(out, force * (1.0f + Fr01() * 2.2f));
      b.vel.y += force * (0.3f + Fr01() * 0.8f);
      b.spin = Vector3{RandRange(-34, 34), RandRange(-34, 34), RandRange(-34, 34)};
      const float s = height * (0.035f + Fr01() * 0.07f);
      b.size = Vector3{s, s * (0.6f + Fr01() * 0.8f), s * (0.7f + Fr01() * 0.9f)};
      b.color = Color{(unsigned char)(96 + GetRandomValue(0, 46)),
                      (unsigned char)(14 + GetRandomValue(0, 16)),
                      (unsigned char)(12 + GetRandomValue(0, 14)), 255};
      b.bleeds = true;
      b.life = 7.0f + Fr01() * 5.0f;
      bodyParts_.push_back(b);
    }
    for (int i = 0; i < 3; ++i)
      BloodSpray(Vector3Add(hitPos, Vector3Scale(RandUnitSphere(), height * 0.2f)),
                 RandUnitSphere(), height * 0.7f);
    // A low red burst so the moment reads even at distance.
    EmitBurst(hitPos, Vector3{0, height * 0.6f, 0}, 16, 0.55f, height * 0.10f,
              height * 0.40f, 0.55f, 0.0f, -4.0f, 0.35f);
    for (size_t i = parts_.size(); i-- > 0 && i + 16 >= parts_.size();)
      parts_[i].base = Color{118, 18, 16, 255};
  }
}

// ------------------------------------------------------------------- update

void FxSystem::Update(float dt, float elapsed, const World& world, Vector3 wind) {
  if (dt <= 0.0f) return;

  for (Particle& p : parts_) {
    p.age += dt;
    p.vel.y += p.buoy * dt;
    const float keep = powf(fmaxf(p.drag, 0.001f), dt);
    p.vel = Vector3Scale(p.vel, keep);
    // Smoke swirls; flashes stay crisp. Strength rises with age so the initial
    // jet reads clean before the cloud breaks up.
    if (p.tint <= 0.5f && !p.noTurb) {
      const float t = elapsed * 0.9f + p.seed * 6.2831853f;
      const float grow = Clampf(p.age / fmaxf(p.life, 0.01f), 0.0f, 1.0f);
      const float amp = 26.0f * (0.25f + grow);
      p.vel.x += sinf(t * 2.1f + p.pos.x * 0.02f) * amp * dt;
      p.vel.z += cosf(t * 1.7f + p.pos.z * 0.02f) * amp * dt;
      p.vel.y += sinf(t * 1.3f + p.seed * 3.0f) * amp * 0.35f * dt;
      p.vel = Vector3Add(p.vel, Vector3Scale(wind, dt * 0.5f));
    }
    p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
    if (p.pos.y < 0.4f && p.tint <= 0.5f) {
      p.pos.y = 0.4f;              // smoke pools on the street
      p.vel.y = fabsf(p.vel.y) * 0.15f;
    }
  }
  // Reap dead particles -- and any whose position has gone non-finite. A single
  // NaN here would make the draw-order comparator inconsistent, and an
  // inconsistent comparator is undefined behaviour in std::sort: in practice it
  // walks off the end of the range and spins.
  parts_.erase(std::remove_if(parts_.begin(), parts_.end(),
                              [](const Particle& p) {
                                return p.age >= p.life ||
                                       !isfinite(p.pos.x) || !isfinite(p.pos.y) ||
                                       !isfinite(p.pos.z);
                              }),
               parts_.end());

  for (DebrisChunk& c : debris_) {
    c.age += dt;
    c.vel.y -= 320.0f * dt;                       // gravity in world units
    c.vel = Vector3Scale(c.vel, powf(c.drag, dt));
    const Vector3 next = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
    // Same rule as body parts: never snap upward onto a roof.
    const float g = world.GroundHeight(next.x, next.z, c.size,
                                       c.pos.y - c.size * 0.5f + 1.0f);
    if (next.y <= g + c.size * 0.5f && c.vel.y < 0.0f) {
      // A case makes a noise the first couple of times it lands, then just
      // skitters -- otherwise a minigun turns the floor into a bell.
      if (c.casing && c.bounces < 2 && c.vel.y < -30.0f)
        casingHits_.push_back(c.pos);
      ++c.bounces;
      c.pos.y = g + c.size * 0.5f;
      c.pos.x = next.x;
      c.pos.z = next.z;
      c.vel.y = -c.vel.y * (c.casing ? 0.42f : 0.32f);   // bounce
      c.vel.x *= c.casing ? 0.72f : 0.55f;
      c.vel.z *= c.casing ? 0.72f : 0.55f;
      c.spin = Vector3Scale(c.spin, 0.5f);
      if (fabsf(c.vel.y) < 12.0f) c.vel.y = 0.0f;
    } else {
      c.pos = next;
    }
    c.angle = Vector3Add(c.angle, Vector3Scale(c.spin, dt));
    if (c.onFire && Fr01() < dt * 26.0f &&
        static_cast<int>(parts_.size()) < kMaxParticles) {
      Particle p;
      p.pos = c.pos;
      p.vel = Vector3{RandRange(-4, 4), 6.0f + Fr01() * 8.0f, RandRange(-4, 4)};
      p.life = 0.35f + Fr01() * 0.4f;
      p.seed = Fr01();
      p.startSize = c.size * 1.4f;
      p.endSize = c.size * 3.0f;
      p.alphaPeak = 0.55f;
      p.tint = Fr01() < 0.4f ? 1.0f : 0.0f;
      p.buoy = 8.0f;
      p.drag = 0.3f;
      p.base = Color{100, 96, 92, 255};
      parts_.push_back(p);
    }
  }
  debris_.erase(std::remove_if(debris_.begin(), debris_.end(),
                               [](const DebrisChunk& c) { return c.age >= c.life; }),
                debris_.end());

  for (Shrapnel& s : shrap_) {
    s.age += dt;
    s.prev = s.pos;
    s.vel.y -= 90.0f * dt;
    s.pos = Vector3Add(s.pos, Vector3Scale(s.vel, dt));
    if (Fr01() < dt * 40.0f && static_cast<int>(parts_.size()) < kMaxParticles) {
      Particle p;
      p.pos = s.pos;
      p.vel = Vector3Scale(s.vel, 0.05f);
      p.life = 0.16f + Fr01() * 0.16f;
      p.seed = Fr01();
      p.startSize = s.size * 1.2f;
      p.endSize = s.size * 2.6f;
      p.alphaPeak = 0.7f;
      p.tint = 1.0f;
      p.drag = 0.2f;
      p.noTurb = true;
      parts_.push_back(p);
    }
  }
  shrap_.erase(std::remove_if(shrap_.begin(), shrap_.end(),
                              [](const Shrapnel& s) { return s.age >= s.life; }),
               shrap_.end());

  for (Tracer& t : tracers_) {
    t.life += dt;
    t.travelled += t.speed * dt;
  }
  tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(),
                                [](const Tracer& t) { return t.life >= t.maxLife; }),
                 tracers_.end());

  // ---- body parts -------------------------------------------------------
  for (BodyPart& b : bodyParts_) {
    b.age += dt;
    if (!b.grounded) {
      b.vel.y -= 300.0f * dt;
      b.vel = Vector3Scale(b.vel, powf(0.72f, dt));
      const Vector3 next = Vector3Add(b.pos, Vector3Scale(b.vel, dt));
      const float half = b.size.y * 0.5f;
      // Only ever land on a surface at or below where the part already is.
      // Passing a generous ceiling here let GroundHeight pick the highest
      // brush top within 500 units *above* the part, so a chunk thrown near a
      // building instantly teleported up onto its roof.
      const float g = world.GroundHeight(next.x, next.z, b.size.x * 0.5f,
                                         b.pos.y - half + 1.0f);
      if (next.y - half <= g && b.vel.y < 0.0f) {
        b.pos.x = next.x;
        b.pos.z = next.z;
        b.pos.y = g + half;
        b.vel.y = -b.vel.y * 0.24f;
        b.vel.x *= 0.42f;
        b.vel.z *= 0.42f;
        b.spin = Vector3Scale(b.spin, 0.4f);
        if (fabsf(b.vel.y) < 14.0f) {
          b.vel = Vector3{0, 0, 0};
          b.spin = Vector3{0, 0, 0};
          b.grounded = true;
          // Settle roughly flat, but keep whatever facing it landed with --
          // snapping every part to the same angle made a row of identical
          // slabs.
          b.angle.x = (b.angle.x > 0 ? 1.0f : -1.0f) * PI * 0.5f +
                      RandRange(-0.35f, 0.35f);
        }
      } else {
        b.pos = next;
      }
      b.angle = Vector3Add(b.angle, Vector3Scale(b.spin, dt));

      // Blood trails off meat while it is still flying.
      if (b.bleeds && Vector3LengthSqr(b.vel) > 900.0f &&
          Fr01() < dt * 30.0f &&
          static_cast<int>(parts_.size()) < kMaxParticles) {
        Particle p;
        p.pos = b.pos;
        p.vel = Vector3Add(Vector3Scale(b.vel, 0.12f),
                           Vector3Scale(RandUnitSphere(), 3.0f));
        p.life = 0.25f + Fr01() * 0.3f;
        p.seed = Fr01();
        p.startSize = b.size.x * 0.35f;
        p.endSize = b.size.x * 0.18f;
        p.alphaPeak = 0.8f;
        p.tint = 0.0f;
        p.buoy = -26.0f;
        p.drag = 0.4f;
        p.base = Color{100, 12, 10, 255};
        p.noTurb = true;
        parts_.push_back(p);
      }
    }
    // Leave a pool where it came to rest.
    if (b.grounded && b.bleeds && b.age < b.life * 0.5f &&
        Fr01() < dt * 1.2f && decals_.size() < 400) {
      AddDecal(Vector3{b.pos.x, b.pos.y - b.size.y * 0.5f + 0.3f, b.pos.z},
               Vector3{0, 1, 0}, b.size.x * (1.0f + Fr01()),
               Color{74, 8, 7, 190}, 30.0f);
    }
  }
  bodyParts_.erase(std::remove_if(bodyParts_.begin(), bodyParts_.end(),
                                  [](const BodyPart& b) { return b.age >= b.life; }),
                   bodyParts_.end());
  for (Decal& d : decals_) d.life += dt;
  decals_.erase(std::remove_if(decals_.begin(), decals_.end(),
                               [](const Decal& d) { return d.life >= d.maxLife; }),
                decals_.end());
}

// --------------------------------------------------------------------- draw

void FxSystem::DrawParticles(const Camera3D& cam) {
  if (parts_.empty()) return;

  // Back-to-front so the alpha-blended smoke composites correctly.
  order_.clear();
  order_.reserve(parts_.size());
  for (size_t i = 0; i < parts_.size(); ++i) {
    float d = Vector3DistanceSqr(parts_[i].pos, cam.position);
    if (!isfinite(d)) d = 0.0f;
    order_.emplace_back(d, static_cast<int>(i));
  }
  // Tie-broken on the index so the ordering is strict and total even when two
  // particles sit at the same distance.
  std::sort(order_.begin(), order_.end(),
            [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
              if (a.first != b.first) return a.first > b.first;
              return a.second < b.second;
            });

  const Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
  Vector3 right = Vector3CrossProduct(fwd, cam.up);
  if (Vector3LengthSqr(right) < 1e-6f) right = Vector3{1, 0, 0};
  right = Vector3Normalize(right);
  const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, fwd));

  rlDisableDepthMask();
  rlDisableBackfaceCulling();

  int mode = -1;   // 0 = alpha, 1 = additive
  auto setMode = [&](int m) {
    if (mode == m) return;
    if (mode >= 0) { rlDrawRenderBatchActive(); EndBlendMode(); }
    BeginBlendMode(m == 1 ? BLEND_ADDITIVE : BLEND_ALPHA);
    mode = m;
  };

  for (const auto& e : order_) {
    const Particle& p = parts_[e.second];
    const float t = Clampf(p.age / fmaxf(p.life, 0.001f), 0.0f, 1.0f);
    const bool fire = p.tint > 0.5f;

    float alpha;
    if (fire) {
      alpha = p.alphaPeak * sinf(PI * powf(t, 0.7f));
    } else {
      const float in = Clampf(t / 0.10f, 0.0f, 1.0f);
      const float out = powf(1.0f - t, 1.7f);
      alpha = p.alphaPeak * in * out;
    }
    if (alpha < 0.004f) continue;

    const float size = p.startSize + (p.endSize - p.startSize) * sqrtf(t);
    const Color col = fire ? FireColor(t, alpha) : SmokeColor(t, alpha, p.base);
    const Texture2D& tex = (fire && p.noTurb && size < 6.0f) ? spark_ : puff_;

    setMode(fire ? 1 : 0);

    // Camera-facing quad with a per-particle roll so puffs are not clones.
    const float rot = p.seed * 6.2831853f + t * (p.seed - 0.5f) * 2.0f;
    const float cs = cosf(rot), sn = sinf(rot);
    const Vector3 rx = Vector3Add(Vector3Scale(right, cs * size),
                                  Vector3Scale(up, sn * size));
    const Vector3 ry = Vector3Add(Vector3Scale(right, -sn * size),
                                  Vector3Scale(up, cs * size));

    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlNormal3f(-fwd.x, -fwd.y, -fwd.z);
    Vector3 v;
    v = Vector3Subtract(Vector3Subtract(p.pos, rx), ry);
    rlTexCoord2f(0, 1); rlVertex3f(v.x, v.y, v.z);
    v = Vector3Add(Vector3Subtract(p.pos, rx), ry);
    rlTexCoord2f(0, 0); rlVertex3f(v.x, v.y, v.z);
    v = Vector3Add(Vector3Add(p.pos, rx), ry);
    rlTexCoord2f(1, 0); rlVertex3f(v.x, v.y, v.z);
    v = Vector3Subtract(Vector3Add(p.pos, rx), ry);
    rlTexCoord2f(1, 1); rlVertex3f(v.x, v.y, v.z);
    rlEnd();
    rlSetTexture(0);
  }

  if (mode >= 0) { rlDrawRenderBatchActive(); EndBlendMode(); }
  rlEnableBackfaceCulling();
  rlEnableDepthMask();
}

void FxSystem::Draw(const Camera3D& cam) {
  // Marks first -- they sit on geometry and should be occluded by it. Bullet
  // holes hold full opacity for most of their life and only fade at the end,
  // so a wall stays visibly chewed up long after the firefight.
  //
  // Culling is off for these. A decal is a single quad whose winding depends
  // on which way its surface normal happens to point, so with backface culling
  // on, marks vanished from every wall whose winding came out clockwise --
  // leaving them visible only at corners, where the adjacent face saved them.
  rlDisableBackfaceCulling();
  BeginBlendMode(BLEND_ALPHA);
  for (const Decal& d : decals_) {
    const float t = d.life / d.maxLife;
    const float fade = d.hole ? Clampf((1.0f - t) * 6.0f, 0.0f, 1.0f)
                              : (1.0f - t);
    // A hole is a couple of units across, so well before the wall it sits on
    // fades into the fog it has already stopped resolving as anything but a
    // dark speck. Fade it out over its own, much shorter range -- otherwise
    // distant walls read as pockmarked from right across the map.
    const float dist = Vector3Distance(d.pos, cam.position);
    const float near = d.hole ? 260.0f : 420.0f;
    const float far = d.hole ? 1050.0f : 1700.0f;
    const float distFade = Clampf((far - dist) / (far - near), 0.0f, 1.0f);
    const float a = fade * distFade * (d.color.a / 255.0f);
    if (a < 0.01f) continue;
    Color c = d.color;
    c.a = static_cast<unsigned char>(a * 255.0f);
    const Vector3 n = d.normal;
    const Vector3 rx = Vector3Scale(d.right, d.halfX);
    const Vector3 ry = Vector3Scale(
        Vector3Normalize(Vector3CrossProduct(n, d.right)), d.halfY);
    rlDisableDepthMask();
    rlSetTexture(d.hole ? hole_.id : puff_.id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlNormal3f(n.x, n.y, n.z);
    Vector3 v;
    v = Vector3Subtract(Vector3Subtract(d.pos, rx), ry);
    rlTexCoord2f(0, 1); rlVertex3f(v.x, v.y, v.z);
    v = Vector3Add(Vector3Subtract(d.pos, rx), ry);
    rlTexCoord2f(0, 0); rlVertex3f(v.x, v.y, v.z);
    v = Vector3Add(Vector3Add(d.pos, rx), ry);
    rlTexCoord2f(1, 0); rlVertex3f(v.x, v.y, v.z);
    v = Vector3Subtract(Vector3Add(d.pos, rx), ry);
    rlTexCoord2f(1, 1); rlVertex3f(v.x, v.y, v.z);
    rlEnd();
    rlSetTexture(0);
  }
  rlDrawRenderBatchActive();
  EndBlendMode();
  rlEnableDepthMask();
  rlEnableBackfaceCulling();

  // Body parts. Anything right on top of the lens is skipped -- a chunk that
  // lands on the camera would otherwise black out the whole screen.
  for (const BodyPart& b : bodyParts_) {
    if (Vector3DistanceSqr(b.pos, cam.position) < 14.0f * 14.0f) continue;
    const float fade = Clampf((b.life - b.age) / 1.2f, 0.0f, 1.0f);
    Color col = b.color;
    col.a = static_cast<unsigned char>(255 * fade);
    rlPushMatrix();
    rlTranslatef(b.pos.x, b.pos.y, b.pos.z);
    rlRotatef(b.angle.y * RAD2DEG, 0, 1, 0);
    rlRotatef(b.angle.x * RAD2DEG, 1, 0, 0);
    rlRotatef(b.angle.z * RAD2DEG, 0, 0, 1);
    if (b.skin && b.skin->id != 0) {
      // Skinned parts keep the sheet's own colours -- tinting them by the team
      // colour turned every limb into a flat blue slab.
      Color white{255, 255, 255, col.a};
      DrawSkinnedBox(Vector3{0, 0, 0}, b.size, *b.skin, b.uv0, b.uv1, white);
    } else {
      DrawCubeV(Vector3{0, 0, 0}, b.size, col);
    }
    rlPopMatrix();
  }

  // Debris chunks.
  for (const DebrisChunk& c : debris_) {
    const float fade = Clampf((c.life - c.age) / 0.8f, 0.0f, 1.0f);
    Color col = c.color;
    col.a = static_cast<unsigned char>(255 * fade);
    rlPushMatrix();
    rlTranslatef(c.pos.x, c.pos.y, c.pos.z);
    rlRotatef(c.angle.y * RAD2DEG, 0, 1, 0);
    rlRotatef(c.angle.x * RAD2DEG, 1, 0, 0);
    rlRotatef(c.angle.z * RAD2DEG, 0, 0, 1);
    DrawCubeV(Vector3{0, 0, 0}, Vector3{c.size, c.size * 0.7f, c.size * 1.3f}, col);
    rlPopMatrix();
  }

  BeginBlendMode(BLEND_ADDITIVE);
  rlDisableDepthMask();

  // Shrapnel streaks -- short bright segments, not full trails.
  for (const Shrapnel& s : shrap_) {
    const float t = s.age / s.life;
    const unsigned char a = static_cast<unsigned char>(220 * (1.0f - t));
    DrawLine3D(s.prev, s.pos, Color{255, 190, 90, a});
  }

  // Tracers: a short streak travelling from muzzle to impact at bullet speed,
  // so the visual arrives when the round does.
  for (const Tracer& t : tracers_) {
    const float head = fminf(t.travelled, t.total);
    const float tail = fmaxf(head - t.streak, 0.0f);
    if (head <= 0.0f || tail >= t.total) continue;
    const Vector3 hp = Vector3Add(t.a, Vector3Scale(t.dir, head));
    const Vector3 tp = Vector3Add(t.a, Vector3Scale(t.dir, tail));

    // Distance haze: a round streaking past a hundred metres away is a faint
    // scratch, not a bright bar.
    const float mid = Vector3Distance(
        Vector3Lerp(tp, hp, 0.5f), cam.position);
    const float fade = Clampf(1.0f - (mid - 220.0f) / 1500.0f, 0.16f, 1.0f);
    // Fade the last moments so it does not simply vanish at the impact.
    const float lifeFade = Clampf((t.maxLife - t.life) / 0.05f, 0.0f, 1.0f);
    const float a = fade * lifeFade;
    if (a < 0.02f) continue;

    Color c = t.color;
    c.a = static_cast<unsigned char>(255 * a);
    DrawLine3D(tp, hp, c);
    const Vector3 o{0.0f, t.width * 0.6f, 0.0f};
    c.a = static_cast<unsigned char>(130 * a);
    DrawLine3D(Vector3Add(tp, o), Vector3Add(hp, o), c);
  }

  rlEnableDepthMask();
  EndBlendMode();

  DrawParticles(cam);
}

}  // namespace kaj
