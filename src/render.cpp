#include "render.h"

#include <algorithm>
#include <cstdio>

#include "fx.h"
#include "rlgl.h"

namespace kaj {
namespace {

// rlgl's immediate mode bakes the model transform into the vertex position, so
// `vertexPosition` is already world space here.
//
// Fog is evaluated per FRAGMENT, not per vertex. The street is a single quad
// spanning the whole map: interpolating a per-vertex fog factor across it made
// the entire ground take the fog value of its four distant corners, which
// washed the texture out completely in heavy weather.
const char* kFogVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorld;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorld = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kFogFS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorld;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 fogColor;
uniform vec3 ambient;
uniform vec3 camPos;
uniform float fogStart;
uniform float fogEnd;
// Up to four transient point lights -- muzzle flashes and explosions. xyz is
// the position, w the radius; the colour carries the intensity.
#define MAX_LIGHTS 4
uniform vec4 lightPosR[MAX_LIGHTS];
uniform vec3 lightColor[MAX_LIGHTS];
uniform int  lightCount;
// Flat mode: every surface reads at its authored texture brightness wherever
// it is on the map and whatever the weather. Distance fog and the sky-driven
// ambient are the two things that make the same wall look different depending
// on where you are standing, so both are switched off here. Muzzle flashes
// and explosions still add light, because those only ever brighten.
uniform int flatLit;
out vec4 finalColor;
void main() {
    vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (c.a < 0.03) discard;
    float d = distance(fragWorld, camPos);
    float fog = flatLit == 1 ? 0.0
              : clamp((d - fogStart) / max(fogEnd - fogStart, 1.0), 0.0, 1.0);

    // Sky-driven ambient, plus whatever is currently burning nearby.
    vec3 light = flatLit == 1 ? vec3(1.0) : ambient;
    for (int i = 0; i < lightCount; ++i) {
        float r = lightPosR[i].w;
        float dist = distance(fragWorld, lightPosR[i].xyz);
        float att = clamp(1.0 - dist / max(r, 1.0), 0.0, 1.0);
        light += lightColor[i] * att * att;
    }
    finalColor = vec4(mix(c.rgb * light, fogColor.rgb, fog), c.a);
}
)";

const Color kTeamColors[] = {
    Color{ 90, 160, 235, 255},   // blue
    Color{235, 130,  60, 255},   // orange
};

// GTJ ran with d3d_set_lighting(false), so every face came out the same
// brightness and box edges were hard to read. A fixed per-normal shade keeps
// the flat look while making corners legible.
inline Color FaceShade(Color c, Vector3 n) {
  float k = 1.0f;
  if (n.y > 0.5f) k = 1.0f;
  else if (n.y < -0.5f) k = 0.55f;
  else if (fabsf(n.x) > 0.5f) k = 0.80f;
  else k = 0.66f;
  return Color{(unsigned char)(c.r * k), (unsigned char)(c.g * k),
               (unsigned char)(c.b * k), c.a};
}

// A single textured quad in world space.
//
// raylib culls back faces (glFrontFace(GL_CCW)), so a quad wound the wrong way
// round simply vanishes -- which is what made some building faces look like
// see-through holes. Rather than hand-verify the winding of all six faces, the
// polygon normal is compared against the face normal here and the vertex order
// is flipped when they disagree.
void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float u, float v,
          Vector3 n, Color tint) {
  const Vector3 poly =
      Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, b));
  // A CCW-wound polygon seen from its +normal side yields cross == +normal;
  // if it comes out negative the order is reversed and GL would cull it.
  const bool flip = Vector3DotProduct(poly, n) < 0.0f;

  const Color col = FaceShade(tint, n);
  rlColor4ub(col.r, col.g, col.b, col.a);
  rlNormal3f(n.x, n.y, n.z);
  if (flip) {
    rlTexCoord2f(u, 0); rlVertex3f(d.x, d.y, d.z);
    rlTexCoord2f(u, v); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(0, v); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(0, 0); rlVertex3f(a.x, a.y, a.z);
  } else {
    rlTexCoord2f(0, 0); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(0, v); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(u, v); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(u, 0); rlVertex3f(d.x, d.y, d.z);
  }
}

const char* WeaponViewmodel(int weapon) {
  return Weapon(weapon).viewmodel;
}

// GTJ drew explosions, grenades and rockets with d3d_draw_ellipsoid: the
// sprite is wrapped around a sphere, which is why its square edges never
// showed. Billboarding those textures instead gives away the quad, so do what
// the original did.
void DrawTexturedSphere(Vector3 c, float r, Texture2D tex, Color col, int rings,
                        int slices) {
  rlDisableBackfaceCulling();
  rlSetTexture(tex.id);
  rlBegin(RL_QUADS);
  rlColor4ub(col.r, col.g, col.b, col.a);
  for (int i = 0; i < rings; ++i) {
    const float v0 = (float)i / rings, v1 = (float)(i + 1) / rings;
    const float p0 = PI * v0, p1 = PI * v1;
    for (int j = 0; j < slices; ++j) {
      const float u0 = (float)j / slices, u1 = (float)(j + 1) / slices;
      const float t0 = 2.0f * PI * u0, t1 = 2.0f * PI * u1;
      auto P = [&](float phi, float th) {
        return Vector3{c.x + r * sinf(phi) * cosf(th), c.y + r * cosf(phi),
                       c.z + r * sinf(phi) * sinf(th)};
      };
      const Vector3 a = P(p0, t0), b = P(p1, t0), d = P(p1, t1), e = P(p0, t1);
      rlNormal3f(0, 1, 0);
      rlTexCoord2f(u0, v0); rlVertex3f(a.x, a.y, a.z);
      rlTexCoord2f(u0, v1); rlVertex3f(b.x, b.y, b.z);
      rlTexCoord2f(u1, v1); rlVertex3f(d.x, d.y, d.z);
      rlTexCoord2f(u1, v0); rlVertex3f(e.x, e.y, e.z);
    }
  }
  rlEnd();
  rlSetTexture(0);
  rlEnableBackfaceCulling();
}

}  // namespace

HudTransform HudTransform::For(int screenW, int screenH) {
  HudTransform t;
  t.scale = static_cast<float>(screenH) / kHudH;
  t.offsetX = (static_cast<float>(screenW) - kHudW * t.scale) * 0.5f;
  t.offsetY = 0.0f;
  return t;
}

bool Renderer::Init() {
  fog_ = LoadShaderFromMemory(kFogVS, kFogFS);
  if (fog_.id == 0) {
    TraceLog(LOG_WARNING, "RENDER: fog shader failed, drawing without fog");
    fogReady_ = false;
    return true;
  }
  locCamPos_ = GetShaderLocation(fog_, "camPos");
  locFogColor_ = GetShaderLocation(fog_, "fogColor");
  locFogStart_ = GetShaderLocation(fog_, "fogStart");
  locFogEnd_ = GetShaderLocation(fog_, "fogEnd");
  locAmbient_ = GetShaderLocation(fog_, "ambient");
  locLightPosR_ = GetShaderLocation(fog_, "lightPosR");
  locLightColor_ = GetShaderLocation(fog_, "lightColor");
  locLightCount_ = GetShaderLocation(fog_, "lightCount");
  locFlatLit_ = GetShaderLocation(fog_, "flatLit");
  fogReady_ = true;
  return true;
}

void Renderer::Shutdown() {
  if (fogReady_) UnloadShader(fog_);
  fogReady_ = false;
}

void Renderer::BeginWorld(const Camera3D& cam, Color fc, float fogStart,
                          float fogEnd, Color amb) {
  BeginMode3D(cam);
  if (!fogReady_) return;
  const float camPos[3] = {cam.position.x, cam.position.y, cam.position.z};
  const float fogCol[4] = {fc.r / 255.0f, fc.g / 255.0f, fc.b / 255.0f, 1.0f};
  const float ambCol[3] = {amb.r / 255.0f, amb.g / 255.0f, amb.b / 255.0f};
  const int flat = flatLit_ ? 1 : 0;
  SetShaderValue(fog_, locFlatLit_, &flat, SHADER_UNIFORM_INT);
  SetShaderValue(fog_, locCamPos_, camPos, SHADER_UNIFORM_VEC3);
  SetShaderValue(fog_, locFogColor_, fogCol, SHADER_UNIFORM_VEC4);
  SetShaderValue(fog_, locFogStart_, &fogStart, SHADER_UNIFORM_FLOAT);
  SetShaderValue(fog_, locFogEnd_, &fogEnd, SHADER_UNIFORM_FLOAT);
  SetShaderValue(fog_, locAmbient_, ambCol, SHADER_UNIFORM_VEC3);

  // Keep the four brightest lights closest to the camera.
  const int kMax = 4;
  if ((int)lights_.size() > kMax) {
    std::partial_sort(
        lights_.begin(), lights_.begin() + kMax, lights_.end(),
        [&](const PointLight& a, const PointLight& b) {
          const float sa = (a.color.x + a.color.y + a.color.z) * a.radius /
                           (1.0f + Vector3Distance(a.pos, cam.position));
          const float sb = (b.color.x + b.color.y + b.color.z) * b.radius /
                           (1.0f + Vector3Distance(b.pos, cam.position));
          return sa > sb;
        });
    lights_.resize(kMax);
  }
  float posR[kMax * 4] = {0};
  float cols[kMax * 3] = {0};
  const int n = (int)lights_.size();
  for (int i = 0; i < n; ++i) {
    posR[i * 4 + 0] = lights_[i].pos.x;
    posR[i * 4 + 1] = lights_[i].pos.y;
    posR[i * 4 + 2] = lights_[i].pos.z;
    posR[i * 4 + 3] = lights_[i].radius;
    cols[i * 3 + 0] = lights_[i].color.x;
    cols[i * 3 + 1] = lights_[i].color.y;
    cols[i * 3 + 2] = lights_[i].color.z;
  }
  SetShaderValueV(fog_, locLightPosR_, posR, SHADER_UNIFORM_VEC4, kMax);
  SetShaderValueV(fog_, locLightColor_, cols, SHADER_UNIFORM_VEC3, kMax);
  SetShaderValue(fog_, locLightCount_, &n, SHADER_UNIFORM_INT);

  BeginShaderMode(fog_);
}

void Renderer::EndWorld() {
  if (fogReady_) EndShaderMode();
  EndMode3D();
}

void Renderer::DrawBrush(const Brush& b, const Assets& assets) {
  const Texture2D& t = assets.Tex(b.tex);
  const float sx = b.max.x - b.min.x;
  const float sy = b.max.y - b.min.y;
  const float sz = b.max.z - b.min.z;
  const float k = b.tile > 0.0f ? b.tile : 32.0f;

  rlSetTexture(t.id);
  rlBegin(RL_QUADS);

  const Vector3 mn = b.min, mx = b.max;
  // -Z and +Z faces (u across X, v down Y)
  Quad({mn.x, mx.y, mn.z}, {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z},
       sx / k, sy / k, {0, 0, -1}, b.tint);
  Quad({mx.x, mx.y, mx.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z},
       sx / k, sy / k, {0, 0, 1}, b.tint);
  // -X and +X faces (u across Z)
  Quad({mn.x, mx.y, mx.z}, {mn.x, mn.y, mx.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z},
       sz / k, sy / k, {-1, 0, 0}, b.tint);
  Quad({mx.x, mx.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z},
       sz / k, sy / k, {1, 0, 0}, b.tint);
  // top (roof) and bottom
  Quad({mn.x, mx.y, mn.z}, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z},
       sx / k, sz / k, {0, 1, 0}, b.tint);
  Quad({mn.x, mn.y, mx.z}, {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z},
       sx / k, sz / k, {0, -1, 0}, b.tint);

  rlEnd();
  rlSetTexture(0);
}

void Renderer::DrawWorld(const World& world, const Assets& assets, Vector3 camPos,
                         float viewDistance) {
  // Street plane, tiled like GTJ's d3d_draw_floor.
  const Texture2D& g = assets.Tex(world.groundTex());
  const float w = world.sizeX(), d = world.sizeZ();
  const float u = w / world.groundTile(), v = d / world.groundTile();
  rlSetTexture(g.id);
  rlBegin(RL_QUADS);
  rlColor4ub(255, 255, 255, 255);
  rlNormal3f(0, 1, 0);
  rlTexCoord2f(0, 0); rlVertex3f(0, 0, 0);
  rlTexCoord2f(0, v); rlVertex3f(0, 0, d);
  rlTexCoord2f(u, v); rlVertex3f(w, 0, d);
  rlTexCoord2f(u, 0); rlVertex3f(w, 0, 0);
  rlEnd();
  rlSetTexture(0);

  // Brushes, culled by the fog distance so the far side of the map is free.
  const float cull = viewDistance + 400.0f;
  for (const Brush& b : world.brushes()) {
    // Vehicles put their bounding box into the world so collision picks them
    // up for free, but they draw themselves oriented -- so skip those here.
    if (b.invisible) continue;
    const Vector3 c{(b.min.x + b.max.x) * 0.5f, (b.min.y + b.max.y) * 0.5f,
                    (b.min.z + b.max.z) * 0.5f};
    const float r = Vector3Length(Vector3Subtract(b.max, c));
    if (Vector3Distance(c, camPos) - r > cull) continue;
    DrawBrush(b, assets);
  }
}

void DrawOrientedBox(Vector3 center, Vector3 size, float yawDeg, Color color) {
  rlPushMatrix();
  rlTranslatef(center.x, center.y, center.z);
  rlRotatef(yawDeg, 0.0f, 1.0f, 0.0f);
  DrawCubeV(Vector3{0, 0, 0}, size, color);
  rlPopMatrix();
}

void Renderer::DrawPlayers(const Client& client, int selfId, const Assets& assets,
                           double renderTime, const Camera3D& cam) {
  tags_.clear();
  const RemotePlayer* ps = client.players();
  for (int i = 0; i < kMaxPlayers; ++i) {
    if (i == selfId || !ps[i].active) continue;
    const PlayerState& s = ps[i].cur;
    if (s.dead()) continue;

    float yaw = 0.0f, pitch = 0.0f;
    const Vector3 feet = ps[i].Interp(renderTime, &yaw, &pitch);
    const float h = s.height();
    const Color team = kTeamColors[s.team % 2];
    const Color dark = Color{(unsigned char)(team.r * 0.55f), (unsigned char)(team.g * 0.55f),
                             (unsigned char)(team.b * 0.55f), 255};

    // A black SWAT operator, built from boxes rather than a sprite so it reads
    // the same from every angle. The team colour is carried by a shoulder flash
    // rather than by painting the whole figure.
    (void)dark;
    rlPushMatrix();
    rlTranslatef(feet.x, feet.y, feet.z);
    rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
    DrawSwatFigure(h, WHITE);
    // Team flash on both shoulders.
    DrawCubeV(Vector3{0.0f, h * 0.735f, -h * 0.238f},
              Vector3{h * 0.08f, h * 0.04f, h * 0.02f}, team);
    DrawCubeV(Vector3{0.0f, h * 0.735f, h * 0.238f},
              Vector3{h * 0.08f, h * 0.04f, h * 0.02f}, team);
    rlPopMatrix();

    // Weapon, held out along the line of aim. Slim, and it kicks back toward
    // the shoulder for a moment each time they fire.
    const Vector3 fwd = ForwardFromAngles(yaw, pitch);
    const Vector3 right = FlatRight(yaw);
    const float kick = (i < 32) ? shotRecoil_[i] : 0.0f;
    const Vector3 hand = Vector3Add(
        Vector3Add(feet, Vector3{0, s.eye() - 3.2f + kick * 0.9f, 0}),
        Vector3Add(Vector3Scale(right, 3.4f),
                   Vector3Scale(fwd, 8.5f - kick * 3.4f)));
    // A thin receiver plus a thinner barrel reaching forward.
    rlPushMatrix();
    rlTranslatef(hand.x, hand.y, hand.z);
    rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
    rlRotatef(-pitch - kick * 9.0f, 0.0f, 0.0f, 1.0f);
    DrawCubeV(Vector3{0.5f, 0, 0}, Vector3{7.0f, 1.9f, 1.7f},
              Color{34, 34, 40, 255});
    DrawCubeV(Vector3{5.5f, 0.1f, 0}, Vector3{7.0f, 0.9f, 0.9f},
              Color{26, 26, 31, 255});
    DrawCubeV(Vector3{-2.6f, -1.1f, 0}, Vector3{3.0f, 2.2f, 1.4f},
              Color{40, 40, 47, 255});
    rlPopMatrix();

    // Queue the floating name tag; text has to be drawn in the 2D pass.
    const Vector3 tagPos{feet.x, feet.y + h + 9.0f, feet.z};
    if (Vector3Distance(cam.position, tagPos) < 1400.0f) {
      // Only if it is actually in front of us.
      const Vector3 toTag = Vector3Subtract(tagPos, cam.position);
      const Vector3 look = Vector3Subtract(cam.target, cam.position);
      if (Vector3DotProduct(toTag, look) > 0.0f) {
        NameTag tag;
        tag.screen = GetWorldToScreen(tagPos, cam);
        tag.name = ps[i].name.empty() ? "player" : ps[i].name;
        tag.health = Clampf(s.health / kMaxHealth, 0.0f, 1.0f);
        tag.color = team;
        tags_.push_back(tag);
      }
    }
  }
}

void Renderer::DrawNameTags() {
  for (const NameTag& t : tags_) {
    const int fw = MeasureText(t.name.c_str(), 14);
    DrawRectangle((int)t.screen.x - fw / 2 - 3, (int)t.screen.y - 20, fw + 6, 16,
                  Color{0, 0, 0, 130});
    DrawText(t.name.c_str(), (int)t.screen.x - fw / 2, (int)t.screen.y - 18, 14,
             t.color);
    const int barW = 34;
    DrawRectangle((int)t.screen.x - barW / 2, (int)t.screen.y - 3, barW, 4,
                  Color{0, 0, 0, 170});
    DrawRectangle((int)t.screen.x - barW / 2, (int)t.screen.y - 3,
                  (int)(barW * t.health), 4, Color{220, 70, 70, 220});
  }
  tags_.clear();
}

void Renderer::DrawEntities(const std::vector<SimEntity>& ents,
                            const Assets& assets, const Camera3D& cam) {
  for (const SimEntity& e : ents) {
    switch (e.kind) {
      case ENT_ROCKET: {
        // A real warhead rather than a textured ball: nose cone, body,
        // fin ring and a burning motor.
        const Vector3 v = e.vel;
        Vector3 fwd = Vector3LengthSqr(v) > 0.01f ? Vector3Normalize(v)
                                                  : Vector3{0, 0, 1};
        const float yaw = atan2f(-fwd.z, fwd.x) * RAD2DEG;
        const float pitch = asinf(Clampf(fwd.y, -1.0f, 1.0f)) * RAD2DEG;
        rlPushMatrix();
        rlTranslatef(e.pos.x, e.pos.y, e.pos.z);
        rlRotatef(yaw, 0, 1, 0);
        rlRotatef(-pitch, 0, 0, 1);   // local +X is forward after the yaw
        // body
        DrawCubeV(Vector3{-1.0f, 0, 0}, Vector3{7.0f, 2.6f, 2.6f},
                  Color{72, 76, 70, 255});
        // nose cone
        DrawCubeV(Vector3{3.4f, 0, 0}, Vector3{2.6f, 1.9f, 1.9f},
                  Color{150, 46, 34, 255});
        DrawCubeV(Vector3{4.9f, 0, 0}, Vector3{1.2f, 1.0f, 1.0f},
                  Color{180, 60, 44, 255});
        // fins
        DrawCubeV(Vector3{-4.0f, 0, 0}, Vector3{2.4f, 5.4f, 0.5f},
                  Color{54, 58, 54, 255});
        DrawCubeV(Vector3{-4.0f, 0, 0}, Vector3{2.4f, 0.5f, 5.4f},
                  Color{54, 58, 54, 255});
        // motor glow
        DrawCubeV(Vector3{-5.4f, 0, 0}, Vector3{1.6f, 2.0f, 2.0f},
                  Color{255, 196, 96, 255});
        rlPopMatrix();
        // The exhaust is a particle trail (see FxSystem::RocketTrail), not a
        // glowing ball stuck to the tail.
        break;
      }
      case ENT_GRENADE: {
        // GTJ3D's tex_grenade wrapped on the body, with a lever and pin so it
        // reads as a grenade rather than a pebble.
        const SpriteSheet& s = assets.Sprite("proj_grenade");
        const float spin = static_cast<float>(e.id) * 37.0f + e.pos.y * 6.0f;
        rlPushMatrix();
        rlTranslatef(e.pos.x, e.pos.y, e.pos.z);
        rlRotatef(spin, 0.3f, 1.0f, 0.2f);
        if (s.valid()) {
          DrawTexturedSphere(Vector3{0, 0, 0}, 3.2f, s.frame(0), WHITE, 7, 9);
        } else {
          DrawSphereEx(Vector3{0, 0, 0}, 3.2f, 7, 9, Color{62, 76, 52, 255});
        }
        DrawCubeV(Vector3{0, 3.3f, 0}, Vector3{1.5f, 1.6f, 1.5f},
                  Color{46, 52, 40, 255});
        DrawCubeV(Vector3{1.3f, 2.6f, 0}, Vector3{0.7f, 3.4f, 1.5f},
                  Color{120, 118, 96, 255});
        rlPopMatrix();
        break;
      }
      case ENT_MINE: {
        DrawCubeV(e.pos, Vector3{9, 3, 9}, Color{60, 60, 68, 255});
        DrawCubeV(Vector3Add(e.pos, Vector3{0, 2.5f, 0}), Vector3{3, 2, 3},
                  Color{255, 40, 40, 255});
        break;
      }
      case ENT_SMOKE: {
        // The canister itself: a grey cylinder-ish body with a spoon and a
        // ring on top, sitting where it landed. It stays for as long as it is
        // venting, so you can see where the cloud is coming from and how much
        // of it is left. In the air it is the same object, tumbling.
        const bool popped = e.arm < 0;
        const Color body = popped ? Color{104, 106, 100, 255}
                                  : Color{86, 92, 74, 255};
        rlPushMatrix();
        rlTranslatef(e.pos.x, e.pos.y, e.pos.z);
        if (!popped) {
          // Tumbling in flight, spun off its own velocity.
          rlRotatef(e.pos.y * 9.0f, 0.3f, 1.0f, 0.2f);
        }
        DrawCubeV(Vector3{0, 2.6f, 0}, Vector3{3.4f, 5.2f, 3.4f}, body);
        // The white band round the middle that says what it is.
        DrawCubeV(Vector3{0, 2.6f, 0}, Vector3{3.6f, 1.3f, 3.6f},
                  Color{214, 214, 208, 255});
        // Fuse assembly and spoon.
        DrawCubeV(Vector3{0, 5.6f, 0}, Vector3{1.6f, 1.4f, 1.6f},
                  Color{54, 56, 58, 255});
        DrawCubeV(Vector3{1.3f, 5.4f, 0}, Vector3{1.0f, 2.6f, 0.7f},
                  Color{70, 72, 74, 255});
        rlPopMatrix();
        break;
      }
      default:
        break;
    }
  }
}

void Renderer::DrawSteeringWheel(const Assets& assets, int turning,
                                 float wheelAngle, float bump,
                                 const HudTransform& hud) {
  const SpriteSheet& s = assets.Sprite("vm_wheel");
  if (!s.valid()) return;
  const Texture2D& tex = s.frame(turning);
  // Sized and placed directly rather than through the sprite origin: this one
  // is a dashboard, not a weapon, so it wants to fill the bottom of the view
  // with a little of it running off the edge.
  const float scale = 1.2f;
  const float w = tex.width * scale, h = tex.height * scale;
  const float vx = kHudW * 0.5f - w * 0.5f;
  const float vy = kHudH + 22.0f - h + bump;
  const Vector2 p = hud.P(vx, vy);
  const Rectangle src{0, 0, (float)tex.width, (float)tex.height};
  // GTJ3D only had the three painted frames. A small roll on top of them,
  // about the hub rather than off the bottom of the sprite, makes the lock
  // read continuously instead of snapping between the three poses.
  const Vector2 pivot{hud.S(w) * 0.5f, hud.S(h) * 0.62f};
  DrawTexturePro(tex, src,
                 Rectangle{p.x + pivot.x, p.y + pivot.y, hud.S(w), hud.S(h)},
                 pivot, -wheelAngle * 0.35f, WHITE);
}

void Renderer::DrawViewmodel(const Assets& assets, int weapon, int animFrame,
                             float bobPhase, float bobAmount, float zoomT,
                             bool dead, float reloadT, float recoilT,
                             const HudTransform& hud) {
  if (dead) return;
  const SpriteSheet& s = assets.Sprite(WeaponViewmodel(weapon));
  if (!s.valid()) return;
  const WeaponDef& d = Weapon(weapon);

  // GTJ drew the viewmodel at (320, 224) in 640x480 ortho, scale 2, sprite
  // origin as authored. Reproduce that, then add bob and a zoom slide-out.
  const float baseScale = d.vmScale;
  const float bobX = sinf(bobPhase) * 7.0f * bobAmount;
  const float bobY = fabsf(cosf(bobPhase)) * 5.0f * bobAmount;
  const float zoomOff = zoomT * 190.0f;

  // ---- reload animation -------------------------------------------------
  // GTJ3D had no reload art, so this is built from the transform: the gun
  // drops out of frame and tilts while the magazine is out, holds low through
  // the middle of the reload, then swings back up and settles with a small
  // overshoot. Shell-fed weapons get a short pump per shell instead.
  float reloadDrop = 0.0f, reloadTilt = 0.0f, reloadSide = 0.0f;
  if (reloadT > 0.0f) {
    const float t = Clampf(reloadT, 0.0f, 1.0f);
    float lower;
    if (t < 0.25f) {
      lower = t / 0.25f;                                   // swing down
    } else if (t < 0.72f) {
      lower = 1.0f;                                        // held low
    } else {
      const float u = (t - 0.72f) / 0.28f;
      lower = (1.0f - u) - sinf(u * PI) * 0.16f;           // up, slight overshoot
    }
    lower = Clampf(lower, -0.2f, 1.0f);
    reloadDrop = lower * 118.0f;
    reloadTilt = lower * 13.0f;
    reloadSide = lower * 26.0f;
    // A bit of shake while the hands are working.
    if (t > 0.25f && t < 0.72f) {
      const float k = (t - 0.25f) / 0.47f;
      reloadDrop += sinf(k * 22.0f) * 5.0f;
      reloadSide += cosf(k * 17.0f) * 4.0f;
    }
  }

  // ---- procedural recoil ------------------------------------------------
  // A sharp kick straight back down the screen with a touch of rise and roll,
  // easing out. Sheets that animate their own recoil carry a small vmRecoil;
  // the single-frame imported models rely on this entirely.
  float kickY = 0.0f, kickX = 0.0f, kickRot = 0.0f;
  if (recoilT > 0.0f && d.vmRecoil > 0.0f) {
    const float t = Clampf(recoilT, 0.0f, 1.0f);
    // Snap out fast, settle slower.
    const float punch = powf(t, 0.55f);
    kickY = punch * d.vmRecoil;
    kickX = punch * d.vmRecoil * 0.18f;
    kickRot = punch * d.vmRecoil * 0.16f;
  }

  const Texture2D& tex = s.frame(animFrame);
  const float w = tex.width * baseScale;
  const float h = tex.height * baseScale;
  const float vx = kHudW * 0.5f - s.origin.x * baseScale + d.vmOffsetX + bobX +
                   reloadSide + kickX;
  // kVmDrop seats every viewmodel a few pixels lower so the hands run off the
  // bottom edge of the screen rather than floating a hair above it.
  constexpr float kVmDrop = 12.0f;
  const float vy = (kHudH - 256.0f) + kVmDrop - s.origin.y * baseScale +
                   d.vmOffsetY + bobY + zoomOff + reloadDrop + kickY;

  const Vector2 p = hud.P(vx, vy);
  const Rectangle src{0, 0, (float)tex.width, (float)tex.height};
  const float tilt = reloadTilt + kickRot;
  if (tilt != 0.0f) {
    // Rotate about the bottom centre of the sprite so it pivots in the hand.
    const Vector2 pivot{hud.S(w) * 0.5f, hud.S(h)};
    DrawTexturePro(tex, src,
                   Rectangle{p.x + pivot.x, p.y + pivot.y, hud.S(w), hud.S(h)},
                   pivot, tilt, WHITE);
  } else {
    DrawTexturePro(tex, src, Rectangle{p.x, p.y, hud.S(w), hud.S(h)},
                   Vector2{0, 0}, 0.0f, WHITE);
  }
}

void Renderer::DrawHud(const Assets& assets, const HudInfo& info,
                       const HudTransform& hud, int screenW, int screenH) {
  const WeaponDef& d = Weapon(info.weapon);

  // ---- sniper scope overlay -------------------------------------------
  if (info.zoomT > 0.02f && d.canZoom) {
    const unsigned char a = static_cast<unsigned char>(235 * info.zoomT);
    const float cx = screenW * 0.5f, cy = screenH * 0.5f;
    const float r = screenH * 0.42f;
    // Vignette the corners so it reads as looking down a scope.
    DrawRectangle(0, 0, screenW, (int)(cy - r), Color{0, 0, 0, a});
    DrawRectangle(0, (int)(cy + r), screenW, screenH, Color{0, 0, 0, a});
    DrawRectangle(0, (int)(cy - r), (int)(cx - r), (int)(2 * r), Color{0, 0, 0, a});
    DrawRectangle((int)(cx + r), (int)(cy - r), screenW, (int)(2 * r),
                  Color{0, 0, 0, a});
    DrawCircleLines((int)cx, (int)cy, r, Color{0, 0, 0, a});
    if (info.zoomT > 0.5f) {
      DrawLine((int)cx, (int)(cy - r), (int)cx, (int)(cy + r), Color{0, 0, 0, 160});
      DrawLine((int)(cx - r), (int)cy, (int)(cx + r), (int)cy, Color{0, 0, 0, 160});
    }
  }

  // ---- crosshair -------------------------------------------------------
  if (info.zoomT < 0.5f && !info.dead) {
    const SpriteSheet& ch = assets.Sprite("crosshair");
    const Vector2 c = hud.P(kHudW * 0.5f, kHudH * 0.5f + 4.0f);
    if (ch.valid()) {
      const Texture2D& t = ch.frame(0);
      const float sz = hud.S(t.width * 1.0f);
      DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height},
                     Rectangle{c.x - sz * 0.5f, c.y - sz * 0.5f, sz, sz},
                     Vector2{0, 0}, 0.0f, Color{255, 255, 255, 190});
    }
    // Dynamic bars that open up with the cone of fire.
    const float gap = hud.S(6.0f + info.spreadDeg * 9.0f);
    const float len = hud.S(7.0f);
    const Color cc{60, 255, 120, 220};
    DrawRectangleV({c.x - gap - len, c.y - 1}, {len, 2}, cc);
    DrawRectangleV({c.x + gap, c.y - 1}, {len, 2}, cc);
    DrawRectangleV({c.x - 1, c.y - gap - len}, {2, len}, cc);
    DrawRectangleV({c.x - 1, c.y + gap}, {2, len}, cc);
  }

  // ---- hit marker ------------------------------------------------------
  if (info.hitMarker > 0.0f) {
    const Vector2 c = hud.P(kHudW * 0.5f, kHudH * 0.5f + 4.0f);
    const float a = Clampf(info.hitMarker / 0.25f, 0.0f, 1.0f);
    const Color hc = info.hitWasHead ? Color{255, 90, 90, (unsigned char)(255 * a)}
                                     : Color{255, 255, 255, (unsigned char)(230 * a)};
    const float o = hud.S(5.0f), l = hud.S(6.0f);
    DrawLineEx({c.x - o - l, c.y - o - l}, {c.x - o, c.y - o}, 2.0f, hc);
    DrawLineEx({c.x + o + l, c.y - o - l}, {c.x + o, c.y - o}, 2.0f, hc);
    DrawLineEx({c.x - o - l, c.y + o + l}, {c.x - o, c.y + o}, 2.0f, hc);
    DrawLineEx({c.x + o + l, c.y + o + l}, {c.x + o, c.y + o}, 2.0f, hc);
  }

  // ---- damage flash (GTJ drew a red rectangle over the whole view) -----
  if (info.damageFlash > 0.0f) {
    // GTJ3D drew a flat red rectangle at alpha 0.3 while hurt_timer ran; this
    // is the same idea, weighted toward the edges so the centre stays readable.
    const float f = Clampf(info.damageFlash, 0.0f, 1.0f);
    const unsigned char a = static_cast<unsigned char>(52 * f);
    DrawRectangle(0, 0, screenW, screenH, Color{190, 24, 24, a});
    const int band = static_cast<int>(screenH * 0.22f);
    const unsigned char e = static_cast<unsigned char>(60 * f);
    DrawRectangleGradientV(0, 0, screenW, band, Color{190, 24, 24, e},
                           Color{190, 24, 24, 0});
    DrawRectangleGradientV(0, screenH - band, screenW, band,
                           Color{190, 24, 24, 0}, Color{190, 24, 24, e});
  }

  // ---- health / armour bars -------------------------------------------
  const Vector2 hb = hud.P(32, kHudH - 24);
  const float bw = hud.S(78), bh = hud.S(19);
  DrawRectangleV(hb, {bw, bh}, Color{0, 0, 0, 190});
  DrawRectangleV({hb.x + 2, hb.y + 2},
                 {(bw - 4) * Clampf(info.health / kMaxHealth, 0, 1), bh - 4},
                 Color{200, 45, 45, 235});
  char buf[96];
  snprintf(buf, sizeof(buf), "%d", (int)(info.health + 0.5f));
  DrawText(buf, (int)(hb.x + 6), (int)(hb.y + 3), (int)hud.S(13), WHITE);

  if (info.armor > 0.0f) {
    const Vector2 ab = hud.P(32, kHudH - 46);
    DrawRectangleV(ab, {bw, hud.S(13)}, Color{0, 0, 0, 190});
    DrawRectangleV({ab.x + 2, ab.y + 2},
                   {(bw - 4) * Clampf(info.armor / kMaxArmor, 0, 1), hud.S(9)},
                   Color{70, 150, 235, 235});
  }

  // ---- weapon / ammo ---------------------------------------------------
  // Stowed while you are driving: the vehicle flies its own readout instead.
  if (info.inVehicle) return;
  const Vector2 ap = hud.P(kHudW - 30, kHudH - 44);
  const char* wname = d.hudName;
  const int nameW = MeasureText(wname, (int)hud.S(15));
  DrawText(wname, (int)(ap.x - nameW), (int)ap.y, (int)hud.S(15),
           Color{225, 225, 235, 255});

  if (d.mode == FIRE_MELEE) {
    snprintf(buf, sizeof(buf), "--");
  } else if (d.magSize == 0) {
    snprintf(buf, sizeof(buf), "x %d", info.reserve);
  } else {
    snprintf(buf, sizeof(buf), "%d / %d", info.mag, info.reserve);
  }
  Color ammoCol = (info.mag == 0 && d.magSize > 0) || (d.magSize == 0 && info.reserve == 0)
                      ? Color{235, 70, 70, 255}
                      : WHITE;
  const int ammoW = MeasureText(buf, (int)hud.S(22));
  DrawText(buf, (int)(ap.x - ammoW), (int)(ap.y + hud.S(17)), (int)hud.S(22), ammoCol);

  if (info.reloading) {
    const char* rl = "RELOADING";
    const int rw = MeasureText(rl, (int)hud.S(16));
    const Vector2 rp = hud.P(kHudW * 0.5f, kHudH * 0.62f);
    DrawText(rl, (int)(rp.x - rw / 2), (int)rp.y, (int)hud.S(16),
             Color{255, 210, 90, 255});
  }

  // ---- weapon strip: number chips along the bottom, selected one named --
  {
    const float chip = hud.S(20), gap = hud.S(4);
    const float total = WEAPON_COUNT * chip + (WEAPON_COUNT - 1) * gap;
    float x = hud.P(kHudW * 0.5f, 0).x - total * 0.5f;
    const float y = hud.P(0, kHudH - 22).y;
    for (int i = 0; i < WEAPON_COUNT; ++i) {
      const bool sel = (i == info.weapon);
      char lbl[8];
      snprintf(lbl, sizeof(lbl), "%c",
               i < 9 ? char('1' + i) : (i == 9 ? '0' : '-'));
      DrawRectangle((int)x, (int)y, (int)chip, (int)chip,
                    sel ? Color{240, 200, 60, 220} : Color{0, 0, 0, 120});
      const int lw = MeasureText(lbl, (int)hud.S(13));
      DrawText(lbl, (int)(x + chip * 0.5f - lw * 0.5f), (int)(y + hud.S(3)),
               (int)hud.S(13), sel ? Color{20, 20, 20, 255} : Color{190, 190, 198, 220});
      x += chip + gap;
    }
  }

  // ---- fps / ping ------------------------------------------------------
  snprintf(buf, sizeof(buf), "Fps:%d", info.fps);
  DrawText(buf, (int)hud.P(10, 6).x, (int)hud.P(0, 6).y, (int)hud.S(12),
           Color{200, 220, 200, 190});
  if (info.ping > 0.0f) {
    snprintf(buf, sizeof(buf), "Ping:%dms", (int)info.ping);
    DrawText(buf, (int)hud.P(10, 20).x, (int)hud.P(0, 20).y, (int)hud.S(12),
             Color{200, 220, 200, 190});
  }

  // ---- kill feed -------------------------------------------------------
  {
    float y = hud.P(0, 40).y;
    for (size_t i = 0; i < info.killFeed.size() && i < 5; ++i) {
      const char* s = info.killFeed[i].c_str();
      const int w = MeasureText(s, (int)hud.S(13));
      DrawRectangle((int)hud.P(kHudW - 12, 0).x - w - (int)hud.S(6), (int)y,
                    w + (int)hud.S(10), (int)hud.S(16), Color{0, 0, 0, 120});
      DrawText(s, (int)hud.P(kHudW - 12, 0).x - w - (int)hud.S(2), (int)(y + hud.S(2)),
               (int)hud.S(13), Color{235, 235, 240, 230});
      y += hud.S(18);
    }
  }

  // ---- centre message --------------------------------------------------
  if (info.messageTime > 0.0f && !info.message.empty()) {
    const int w = MeasureText(info.message.c_str(), (int)hud.S(18));
    const Vector2 p = hud.P(kHudW * 0.5f, kHudH * 0.42f);
    DrawText(info.message.c_str(), (int)(p.x - w / 2 + 2), (int)p.y + 2,
             (int)hud.S(18), Color{0, 0, 0, 200});
    DrawText(info.message.c_str(), (int)(p.x - w / 2), (int)p.y, (int)hud.S(18),
             Color{255, 235, 160, 255});
  }

  // ---- death overlay ---------------------------------------------------
  if (info.dead) {
    DrawRectangle(0, 0, screenW, screenH, Color{80, 0, 0, 90});
    const char* t = "YOU DIED";
    const int w = MeasureText(t, (int)hud.S(40));
    const Vector2 p = hud.P(kHudW * 0.5f, kHudH * 0.38f);
    DrawText(t, (int)(p.x - w / 2), (int)p.y, (int)hud.S(40),
             Color{240, 220, 220, 240});
    snprintf(buf, sizeof(buf), "Respawning in %.1f", info.respawnIn);
    const int w2 = MeasureText(buf, (int)hud.S(18));
    DrawText(buf, (int)(p.x - w2 / 2), (int)(p.y + hud.S(48)), (int)hud.S(18),
             Color{230, 230, 230, 220});
  }
}

void Renderer::DrawScoreboard(const Client& client, int selfId,
                              const HudTransform& hud) {
  struct Row { int id; int kills; int deaths; const std::string* name; };
  std::vector<Row> rows;
  const RemotePlayer* ps = client.players();
  for (int i = 0; i < kMaxPlayers; ++i) {
    if (!ps[i].active) continue;
    rows.push_back({i, ps[i].cur.kills, ps[i].cur.deaths, &ps[i].name});
  }
  std::sort(rows.begin(), rows.end(),
            [](const Row& a, const Row& b) { return a.kills > b.kills; });

  const float w = hud.S(420), h = hud.S(60 + 22 * (float)(rows.size() + 1));
  const Vector2 o = hud.P(kHudW * 0.5f - 210.0f, 70.0f);
  DrawRectangleV(o, {w, h}, Color{10, 12, 16, 225});
  DrawRectangleLinesEx({o.x, o.y, w, h}, 2.0f, Color{200, 170, 60, 200});
  DrawText("SCOREBOARD", (int)(o.x + hud.S(14)), (int)(o.y + hud.S(10)),
           (int)hud.S(20), Color{240, 210, 90, 255});
  DrawText("PLAYER", (int)(o.x + hud.S(14)), (int)(o.y + hud.S(38)),
           (int)hud.S(13), Color{170, 170, 180, 255});
  DrawText("K", (int)(o.x + w - hud.S(90)), (int)(o.y + hud.S(38)),
           (int)hud.S(13), Color{170, 170, 180, 255});
  DrawText("D", (int)(o.x + w - hud.S(46)), (int)(o.y + hud.S(38)),
           (int)hud.S(13), Color{170, 170, 180, 255});

  float y = o.y + hud.S(58);
  char buf[32];
  for (const Row& r : rows) {
    const bool me = (r.id == selfId);
    if (me) DrawRectangleV({o.x + 4, y - 2}, {w - 8, hud.S(20)},
                           Color{240, 200, 60, 45});
    const char* nm = r.name->empty() ? "player" : r.name->c_str();
    DrawText(nm, (int)(o.x + hud.S(14)), (int)y, (int)hud.S(15),
             me ? Color{255, 225, 120, 255} : Color{225, 225, 230, 255});
    snprintf(buf, sizeof(buf), "%d", r.kills);
    DrawText(buf, (int)(o.x + w - hud.S(90)), (int)y, (int)hud.S(15), WHITE);
    snprintf(buf, sizeof(buf), "%d", r.deaths);
    DrawText(buf, (int)(o.x + w - hud.S(46)), (int)y, (int)hud.S(15),
             Color{200, 160, 160, 255});
    y += hud.S(22);
  }
}

}  // namespace kaj
