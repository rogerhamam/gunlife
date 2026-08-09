#include "sky.h"

#include "assets.h"
#include "rlgl.h"

namespace kaj {
namespace {

// Both passes are fullscreen: raylib hands us a screen-space quad, we turn the
// fragment's NDC back into a world ray with the inverse view-projection.
const char* kFullscreenVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 fragTexCoord;
void main() {
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kSkyFS = R"(#version 330
in vec2 fragTexCoord;
uniform mat4  uInvVP;
uniform vec3  uSunDir;
uniform vec3  uHorizon;
uniform vec3  uZenith;
uniform float uStorm;
uniform vec2  uCloudWind;
uniform float uLightning;
uniform float uTime;
out vec4 finalColor;

float hash2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float a = hash2(i),                  b = hash2(i + vec2(1.0, 0.0));
    float c = hash2(i + vec2(0.0, 1.0)), d = hash2(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0, amp = 0.5;
    for (int i = 0; i < 5; ++i) { v += vnoise(p) * amp; p *= 2.1; amp *= 0.5; }
    return v;
}

void main() {
    // fragTexCoord.y runs 0 at the TOP of the quad, but NDC y is +1 at the top.
    // Without this flip the whole sky renders upside down -- zenith at the
    // horizon, ground haze overhead, and rain that appears to fall upward.
    vec2 ndc = vec2(fragTexCoord.x * 2.0 - 1.0, 1.0 - fragTexCoord.y * 2.0);
    vec4 nh = uInvVP * vec4(ndc, -1.0, 1.0);
    vec4 fh = uInvVP * vec4(ndc,  1.0, 1.0);
    vec3 dir = normalize(fh.xyz / fh.w - nh.xyz / nh.w);
    float y = clamp(dir.y, -1.0, 1.0);

    vec3 col;
    if (y >= 0.0) {
        col = mix(uHorizon, uZenith, smoothstep(0.0, 0.6, y));
    } else {
        // Below the horizon is haze over the street; blend down into it so the
        // world's fog meets the sky without a seam.
        vec3 ground = uHorizon * 0.42;
        col = mix(uHorizon, ground, smoothstep(0.0, 0.30, -y));
    }

    col = mix(col, vec3(0.18, 0.20, 0.24), uStorm * 0.55);

    float sunDot = dot(dir, normalize(uSunDir));
    if (uSunDir.y > -0.05) {
        float disc      = smoothstep(0.9994, 0.9998, sunDot);
        float tightHalo = pow(max(sunDot, 0.0), 80.0) * 0.55;
        float wideHalo  = pow(max(sunDot, 0.0), 6.0)  * 0.18;
        float vis = clamp(uSunDir.y * 5.0 + 0.6, 0.0, 1.0) * (1.0 - uStorm * 0.85);
        col = mix(col, vec3(1.0, 0.96, 0.78), disc * vis);
        col += vec3(1.0, 0.72, 0.42) * tightHalo * vis;
        col += vec3(1.0, 0.55, 0.30) * wideHalo  * vis;
    }

    // Moon, opposite the sun.
    {
        vec3 moonDir = -normalize(uSunDir);
        if (moonDir.y > -0.05) {
            float md = dot(dir, moonDir);
            float disc = smoothstep(0.9990, 0.9996, md);
            float halo = pow(max(md, 0.0), 220.0) * 0.35;
            float vis = clamp(-uSunDir.y * 4.0 + 0.3, 0.0, 1.0) * (1.0 - uStorm * 0.7);
            col = mix(col, vec3(0.92, 0.94, 0.99), disc * vis);
            col += vec3(0.55, 0.62, 0.80) * halo * vis;
        }
    }

    // Clouds that form, drift and dissipate.
    //
    // The layer is a flat plane overhead, so the UV is the view ray's
    // intersection with it: dir.xz / dir.y. Clamping dir.y (the old
    // max(y,0.22)) broke that projection below ~13 degrees, which made the
    // cloud field slide and smear as the camera pitched -- the sky appeared
    // glued to the view. Now the true reciprocal is used and the layer is
    // simply faded out before the projection degenerates at the horizon.
    if (y > 0.03) {
        float dy = y;
        vec2 cuv = dir.xz / dy * 0.30 + uCloudWind + vec2(uTime * 0.0015, uTime * 0.0010);
        float cloud = fbm(cuv);
        float densPulse = fbm(cuv * 0.45 + vec2(uTime * 0.012, -uTime * 0.009));
        float thresh = mix(0.55, 0.10, uStorm) - (densPulse - 0.5) * 0.30;
        float mask = smoothstep(thresh, thresh + 0.18, cloud);
        if (uStorm > 0.05) {
            float storm2 = fbm(cuv * 0.85 + vec2(uTime * 0.0035, -uTime * 0.0025));
            mask = max(mask, smoothstep(0.30, 0.55, storm2) * uStorm);
        }
        mask *= smoothstep(0.05, 0.34, y);   // hide the degenerate horizon band
        vec3 cloudBase = mix(vec3(0.96, 0.94, 0.90), vec3(0.32, 0.34, 0.38), uStorm);
        float dayFactor = smoothstep(-0.10, 0.45, uSunDir.y);
        vec3 light = mix(uHorizon * 1.10, cloudBase, dayFactor * dayFactor);
        light = mix(light, uZenith * 0.70 + cloudBase * 0.30, dayFactor * 0.20);
        vec3 shade = light * 0.55;
        float dens = smoothstep(0.0, 0.55, fbm(cuv * 1.7 + 1.3));
        col = mix(col, mix(light, shade, dens * (0.4 + uStorm * 0.5)),
                  mask * (0.80 + uStorm * 0.15));
    }

    // Stars once the sun is well down. Each cell holds at most one star at a
    // jittered position, drawn as a round point -- sampling the cell directly
    // made them stretch into dashes toward the horizon.
    if (uSunDir.y < 0.05 && y > 0.06) {
        float night = clamp(-uSunDir.y * 5.0, 0.0, 1.0) * (1.0 - uStorm);
        if (night > 0.05) {
            vec2 suv  = dir.xz / y * 34.0;
            vec2 cell = floor(suv);
            vec2 f    = fract(suv);
            float st  = hash2(cell);
            if (st > 0.988) {
                vec2 jit = vec2(hash2(cell + 1.7), hash2(cell + 3.3));
                float d  = length(f - jit);
                float tw = smoothstep(0.13, 0.0, d) *
                           (0.55 + 0.45 * sin(uTime * 2.4 + st * 60.0));
                // Fade out near the horizon where the projection smears.
                col += vec3(0.90, 0.93, 1.0) * tw * night *
                       smoothstep(0.10, 0.35, y);
            }
        }
    }

    col += vec3(0.65, 0.78, 1.0) * uLightning * 0.7;
    finalColor = vec4(col, 1.0);
}
)";

const char* kRainFS = R"(#version 330
in vec2 fragTexCoord;
uniform mat4  uInvVP;
uniform float uTime;
uniform float uIntensity;
uniform float uHeavy;
uniform vec3  uWind3D;
out vec4 finalColor;
float h(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    if (uIntensity < 0.01) discard;
    vec2 ndc = vec2(fragTexCoord.x * 2.0 - 1.0, 1.0 - fragTexCoord.y * 2.0);
    vec4 nh = uInvVP * vec4(ndc, -1.0, 1.0);
    vec4 fh = uInvVP * vec4(ndc,  1.0, 1.0);
    vec3 dir = normalize(fh.xyz / fh.w - nh.xyz / nh.w);

    float belowFade = smoothstep(-0.55, -0.10, dir.y);
    float horizonFade = mix(belowFade, 1.0, smoothstep(-0.10, 0.20, dir.y));
    if (horizonFade < 0.005) discard;

    float horAng = atan(dir.x, dir.z);
    vec3 viewRight = normalize(cross(dir, vec3(0.0, 1.0, 0.0)));
    float windSlant = clamp(dot(uWind3D, viewRight) * 0.02, -0.35, 0.35);
    float tilt = 0.18 + windSlant;

    float vy = clamp(dir.y, -0.50, 1.0);
    float vyMapped = sign(vy) * sqrt(abs(vy));

    float alphaSum = 0.0;
    for (int layer = 0; layer < 3; ++layer) {
        float lf = float(layer);
        vec2 uv;
        uv.x = horAng * (90.0 + lf * 25.0);
        uv.y = vyMapped * (22.0 + lf * 5.0);
        uv.x += uv.y * tilt;
        // uv.y grows with elevation, so ADDING time slides each streak toward
        // lower elevation, i.e. downward. (The rain only looked inverted
        // because the whole ray direction was flipped; see the ndc fix above.)
        uv.y += uTime * (28.0 + lf * 6.0);
        uv.x += uWind3D.x * uTime * 0.03;

        vec2 cell = floor(uv);
        vec2 fr   = fract(uv);
        float rcell = h(cell + vec2(lf * 17.3, 4.1));
        float density = uIntensity * (0.45 + uHeavy * 0.40) + 0.05;
        if (rcell > density) continue;
        float jx = h(cell + vec2(7.7, lf * 3.1));
        float jy = h(cell + vec2(13.1, lf * 5.7));
        float dx = abs(fr.x - (0.15 + jx * 0.70));
        float halfW = 0.020 + lf * 0.010 + uHeavy * 0.012;
        if (dx > halfW) continue;
        float edge = smoothstep(halfW, halfW * 0.4, dx);
        // Brightest at the leading (lower) end of the streak, tail trailing up.
        float bright = 1.0 - fract(fr.y + jy);
        alphaSum += bright * edge * (0.28 - lf * 0.05);
    }
    float a = clamp(alphaSum * uIntensity * horizonFade, 0.0, 0.55 + uHeavy * 0.30);
    if (a < 0.005) discard;
    finalColor = vec4(0.86, 0.90, 0.98, a);
}
)";

Matrix InverseViewProjection(const Camera3D& cam, int w, int h) {
  const Matrix view = GetCameraMatrix(cam);
  const float aspect = (h > 0) ? (float)w / (float)h : 1.7778f;
  const Matrix proj =
      MatrixPerspective(cam.fovy * DEG2RAD, aspect, RL_CULL_DISTANCE_NEAR,
                        RL_CULL_DISTANCE_FAR);
  return MatrixInvert(MatrixMultiply(view, proj));
}

void DrawFullscreen(Shader s, int w, int h) {
  BeginShaderMode(s);
  // A plain white texture stretched over the viewport; the shader ignores it
  // and works entirely from fragTexCoord.
  rlSetTexture(rlGetTextureIdDefault());
  rlBegin(RL_QUADS);
  rlColor4ub(255, 255, 255, 255);
  rlNormal3f(0, 0, 1);
  rlTexCoord2f(0, 0); rlVertex2f(0, 0);
  rlTexCoord2f(0, 1); rlVertex2f(0, (float)h);
  rlTexCoord2f(1, 1); rlVertex2f((float)w, (float)h);
  rlTexCoord2f(1, 0); rlVertex2f((float)w, 0);
  rlEnd();
  rlSetTexture(0);
  EndShaderMode();
}

}  // namespace

bool Sky::Init() {
  sky_ = LoadShaderFromMemory(kFullscreenVS, kSkyFS);
  rainShader_ = LoadShaderFromMemory(kFullscreenVS, kRainFS);
  if (sky_.id == 0) {
    TraceLog(LOG_WARNING, "SKY: shader failed to compile, using a flat sky");
    return false;
  }
  locInvVP_ = GetShaderLocation(sky_, "uInvVP");
  locSunDir_ = GetShaderLocation(sky_, "uSunDir");
  locHorizon_ = GetShaderLocation(sky_, "uHorizon");
  locZenith_ = GetShaderLocation(sky_, "uZenith");
  locStorm_ = GetShaderLocation(sky_, "uStorm");
  locCloudWind_ = GetShaderLocation(sky_, "uCloudWind");
  locLightning_ = GetShaderLocation(sky_, "uLightning");
  locTime_ = GetShaderLocation(sky_, "uTime");

  rLocInvVP_ = GetShaderLocation(rainShader_, "uInvVP");
  rLocTime_ = GetShaderLocation(rainShader_, "uTime");
  rLocIntensity_ = GetShaderLocation(rainShader_, "uIntensity");
  rLocHeavy_ = GetShaderLocation(rainShader_, "uHeavy");
  rLocWind_ = GetShaderLocation(rainShader_, "uWind3D");

  ready_ = true;
  UpdatePalette();
  return true;
}

void Sky::Shutdown() {
  if (!ready_) return;
  UnloadShader(sky_);
  UnloadShader(rainShader_);
  ready_ = false;
}

const char* Sky::presetName() const {
  switch (preset_) {
    case WeatherPreset::Clear: return "CLEAR";
    case WeatherPreset::Fair: return "FAIR";
    case WeatherPreset::Overcast: return "OVERCAST";
    case WeatherPreset::Storm: return "STORM";
    case WeatherPreset::Night: return "NIGHT";
    case WeatherPreset::Cycle: return "CYCLE";
  }
  return "?";
}

const char* Sky::bandName() const {
  switch (band_) {
    case WeatherBand::Clear: return "CLEAR";
    case WeatherBand::Fair: return "FAIR";
    case WeatherBand::Overcast: return "OVERCAST";
    case WeatherBand::Storm: return "STORM";
    default: break;
  }
  return "?";
}

// How likely each band is on any given roll, and the storm value it means.
//
// Rain starts above a storm value of 0.28, so Overcast and Storm are the wet
// bands and Clear and Fair are dry. Those two used to carry 22 + 16 = 38 of
// the 100 weight between them; they carry 11 + 8 = 19 now, which is exactly
// half as much rain, with what they gave up going to Clear and Fair. Fair's
// ceiling also comes down from 0.30 to 0.26 so that "fair" means genuinely
// dry rather than a trace of drizzle at the top of its range.
//
// The range on each is what stops two overcast spells looking identical.
namespace {
struct BandOdds {
  WeatherBand band;
  int weight;
  float stormLo, stormHi;
};
const BandOdds kBands[] = {
    {WeatherBand::Clear,    40, 0.00f, 0.06f},
    {WeatherBand::Fair,     41, 0.12f, 0.26f},
    {WeatherBand::Overcast, 11, 0.42f, 0.62f},
    {WeatherBand::Storm,     8, 0.78f, 1.00f},
};
}  // namespace

void Sky::RollWeather() {
  int total = 0;
  for (const BandOdds& b : kBands) total += b.weight;

  const WeatherBand was = band_;
  for (int attempt = 0; attempt < 2; ++attempt) {
    int roll = GetRandomValue(0, total - 1);
    for (const BandOdds& b : kBands) {
      roll -= b.weight;
      if (roll >= 0) continue;
      band_ = b.band;
      stormTarget_ = RandRange(b.stormLo, b.stormHi);
      break;
    }
    // One re-roll if it came up the same, so a change of phase is a change of
    // weather rather than sometimes being nothing at all.
    if (band_ != was) break;
  }
}

void Sky::SetPreset(WeatherPreset p) {
  preset_ = p;
  dayRate_ = 0.0f;
  switch (p) {
    case WeatherPreset::Clear:
      stormTarget_ = 0.0f;  timeOfDay_ = 0.46f; band_ = WeatherBand::Clear;
      break;
    case WeatherPreset::Fair:
      stormTarget_ = 0.18f; timeOfDay_ = 0.38f; band_ = WeatherBand::Fair;
      break;
    case WeatherPreset::Overcast:
      stormTarget_ = 0.55f; timeOfDay_ = 0.44f; band_ = WeatherBand::Overcast;
      break;
    case WeatherPreset::Storm:
      stormTarget_ = 1.0f;  timeOfDay_ = 0.40f; band_ = WeatherBand::Storm;
      break;
    case WeatherPreset::Night:
      stormTarget_ = 0.10f; timeOfDay_ = 0.92f; band_ = WeatherBand::Clear;
      break;
    case WeatherPreset::Cycle:
      // A fixed ten-minute day, and a fresh roll of the weather every hundred
      // seconds of it. Neither drifts: the clock is the same length whatever
      // the sky happens to be doing.
      dayRate_ = 1.0f / kDayCycleSeconds;
      phaseTimer_ = kWeatherPhaseSeconds;
      RollWeather();
      break;
  }
}

void Sky::UpdatePalette() {
  // Sun sweeps a tilted arc so it rises and sets rather than passing overhead.
  const float a = (timeOfDay_ - 0.25f) * 2.0f * PI;
  sunDir_ = Vector3Normalize(Vector3{cosf(a) * 0.75f, sinf(a), 0.36f});

  // Three palettes blended by sun height: night, golden hour, day.
  const float hgt = sunDir_.y;
  const Vector3 nightH{0.06f, 0.08f, 0.14f}, nightZ{0.015f, 0.02f, 0.06f};
  const Vector3 duskH{0.86f, 0.46f, 0.24f}, duskZ{0.16f, 0.22f, 0.44f};
  const Vector3 dayH{0.66f, 0.74f, 0.84f}, dayZ{0.20f, 0.40f, 0.72f};

  if (hgt < 0.0f) {
    const float k = Clampf((hgt + 0.25f) / 0.25f, 0.0f, 1.0f);
    horizon_ = Vector3Lerp(nightH, duskH, k);
    zenith_ = Vector3Lerp(nightZ, duskZ, k);
  } else {
    const float k = Clampf(hgt / 0.32f, 0.0f, 1.0f);
    horizon_ = Vector3Lerp(duskH, dayH, k);
    zenith_ = Vector3Lerp(duskZ, dayZ, k);
  }
  // Storms desaturate toward slate.
  const Vector3 slate{0.30f, 0.33f, 0.37f};
  horizon_ = Vector3Lerp(horizon_, slate, storm_ * 0.55f);
  zenith_ = Vector3Lerp(zenith_, Vector3Scale(slate, 0.7f), storm_ * 0.55f);
}

void Sky::Update(float dt, Assets& assets, Vector3 listener) {
  elapsed_ += dt;

  if (preset_ == WeatherPreset::Cycle) {
    timeOfDay_ = fmodf(timeOfDay_ + dayRate_ * dt, 1.0f);
    phaseTimer_ -= dt;
    if (phaseTimer_ <= 0.0f) {
      // Add rather than assign, so a long frame does not stretch the clock:
      // six rolls still land in exactly six hundred seconds.
      phaseTimer_ += kWeatherPhaseSeconds;
      if (phaseTimer_ <= 0.0f) phaseTimer_ = kWeatherPhaseSeconds;
      RollWeather();
    }
  }
  // The front moves in over roughly twenty seconds, so a roll reads as
  // weather closing in rather than as the sky changing between frames.
  storm_ += (stormTarget_ - storm_) * fminf(1.0f, dt * 0.25f);
  rain_ = Clampf((storm_ - 0.28f) / 0.72f, 0.0f, 1.0f);

  windAngle_ += dt * 0.05f;
  windStrength_ = 16.0f + storm_ * 130.0f;
  cloudWindX_ += cosf(windAngle_) * dt * 0.0032f * (0.5f + storm_);
  cloudWindY_ += sinf(windAngle_) * dt * 0.0032f * (0.5f + storm_);

  UpdatePalette();

  // ---- lightning + thunder --------------------------------------------
  lightning_ *= powf(0.02f, dt);        // fast decay
  if (lightning_ < 0.002f) lightning_ = 0.0f;
  if (storm_ > 0.45f) {
    nextStrike_ -= dt * (storm_ - 0.35f) * 3.0f;
    if (nextStrike_ <= 0.0f) {
      nextStrike_ = RandRange(3.0f, 14.0f) * (1.4f - storm_);
      lightning_ = RandRange(0.55f, 1.0f);
      // Thunder trails the flash by distance / speed of sound.
      const float distKm = RandRange(0.2f, 3.5f) * (1.4f - storm_);
      thunderDelay_ = distKm * 2.9f;
      thunderVolume_ = Clampf(0.85f - distKm * 0.18f, 0.12f, 0.9f);
    }
  } else {
    nextStrike_ = RandRange(6.0f, 20.0f);
  }
  if (thunderDelay_ > 0.0f) {
    thunderDelay_ -= dt;
    if (thunderDelay_ <= 0.0f) {
      thunderDelay_ = -1.0f;
      assets.Play("thunder", thunderVolume_, 0.5f, RandRange(0.85f, 1.05f));
    }
  }

  // ---- ambient beds ----------------------------------------------------
  // The weather bed -- wind and rain -- at 0.54 of what it originally was:
  // two thirds, then a further fifth off that. At full volume a storm sat on
  // top of the gunfire instead of behind it.
  constexpr float kWeatherVolume = 0.67f * 0.80f;
  const float windVol =
      Clampf(0.05f + storm_ * 0.35f, 0.0f, 0.45f) * kWeatherVolume;
  assets.PlayLoop("wind", windVol);
  assets.SetLoopVolume("wind", windVol);
  (void)listener;
}

Color Sky::horizonColor() const {
  return Color{(unsigned char)(Clampf(horizon_.x, 0, 1) * 255),
               (unsigned char)(Clampf(horizon_.y, 0, 1) * 255),
               (unsigned char)(Clampf(horizon_.z, 0, 1) * 255), 255};
}

Color Sky::fogColor() const {
  // Fog sits a touch darker than the horizon so distant geometry reads as
  // silhouette rather than dissolving into the sky exactly.
  const Vector3 f = Vector3Scale(horizon_, 0.82f);
  return Color{(unsigned char)(Clampf(f.x, 0, 1) * 255),
               (unsigned char)(Clampf(f.y, 0, 1) * 255),
               (unsigned char)(Clampf(f.z, 0, 1) * 255), 255};
}

Color Sky::ambientColor() const {
  const float h = sunDir_.y;
  // 0 at night, 1 with the sun well up.
  const float day = Clampf((h + 0.12f) / 0.42f, 0.0f, 1.0f);
  const Vector3 night{0.26f, 0.30f, 0.44f};
  const Vector3 dusk{1.05f, 0.80f, 0.62f};
  const Vector3 noon{1.05f, 1.03f, 0.98f};
  Vector3 c;
  if (day < 0.45f) c = Vector3Lerp(night, dusk, day / 0.45f);
  else c = Vector3Lerp(dusk, noon, (day - 0.45f) / 0.55f);
  // Storms kill the sun and drain the colour.
  const float s = storm_;
  c = Vector3Lerp(c, Vector3{0.52f, 0.56f, 0.60f}, s * 0.75f);
  c = Vector3Scale(c, 1.0f - s * 0.20f);
  // Lightning briefly floods the street.
  c = Vector3Add(c, Vector3Scale(Vector3{0.5f, 0.6f, 0.8f}, lightning_ * 0.8f));
  return Color{(unsigned char)(Clampf(c.x, 0, 1) * 255),
               (unsigned char)(Clampf(c.y, 0, 1) * 255),
               (unsigned char)(Clampf(c.z, 0, 1) * 255), 255};
}

float Sky::fogScale() const {
  // Heavy weather closes the view distance to ~45%.
  return 1.0f - storm_ * 0.55f;
}

Vector3 Sky::wind() const {
  return Vector3{cosf(windAngle_) * windStrength_, 0.0f,
                 sinf(windAngle_) * windStrength_};
}

void Sky::DrawSky(const Camera3D& cam, int screenW, int screenH) {
  if (!ready_) return;
  const Matrix inv = InverseViewProjection(cam, screenW, screenH);
  const float sun[3] = {sunDir_.x, sunDir_.y, sunDir_.z};
  const float hor[3] = {horizon_.x, horizon_.y, horizon_.z};
  const float zen[3] = {zenith_.x, zenith_.y, zenith_.z};
  const float cw[2] = {cloudWindX_, cloudWindY_};
  SetShaderValueMatrix(sky_, locInvVP_, inv);
  SetShaderValue(sky_, locSunDir_, sun, SHADER_UNIFORM_VEC3);
  SetShaderValue(sky_, locHorizon_, hor, SHADER_UNIFORM_VEC3);
  SetShaderValue(sky_, locZenith_, zen, SHADER_UNIFORM_VEC3);
  SetShaderValue(sky_, locStorm_, &storm_, SHADER_UNIFORM_FLOAT);
  SetShaderValue(sky_, locCloudWind_, cw, SHADER_UNIFORM_VEC2);
  SetShaderValue(sky_, locLightning_, &lightning_, SHADER_UNIFORM_FLOAT);
  SetShaderValue(sky_, locTime_, &elapsed_, SHADER_UNIFORM_FLOAT);
  DrawFullscreen(sky_, screenW, screenH);
}

void Sky::DrawRain(const Camera3D& cam, int screenW, int screenH) {
  // Under a roof there is no rain. The value is eased so stepping through a
  // doorway fades rather than snaps.
  const float wet = rain_ * (1.0f - shelter_);
  if (!ready_ || wet < 0.02f) return;
  const Matrix inv = InverseViewProjection(cam, screenW, screenH);
  const Vector3 w = wind();
  const float wind3[3] = {w.x, w.y, w.z};
  const float heavy = Clampf((storm_ - 0.6f) / 0.4f, 0.0f, 1.0f);
  SetShaderValueMatrix(rainShader_, rLocInvVP_, inv);
  SetShaderValue(rainShader_, rLocTime_, &elapsed_, SHADER_UNIFORM_FLOAT);
  SetShaderValue(rainShader_, rLocIntensity_, &wet, SHADER_UNIFORM_FLOAT);
  SetShaderValue(rainShader_, rLocHeavy_, &heavy, SHADER_UNIFORM_FLOAT);
  SetShaderValue(rainShader_, rLocWind_, wind3, SHADER_UNIFORM_VEC3);
  DrawFullscreen(rainShader_, screenW, screenH);
}

void Sky::DrawLightningFlash(int screenW, int screenH) {
  if (lightning_ < 0.02f) return;
  const unsigned char a = static_cast<unsigned char>(
      Clampf(lightning_, 0.0f, 1.0f) * 110.0f);
  DrawRectangle(0, 0, screenW, screenH, Color{200, 220, 255, a});
}

}  // namespace kaj
