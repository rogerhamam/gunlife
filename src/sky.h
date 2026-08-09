// Sky and weather, ported from Naval Command.
//
// The sky is a fullscreen ray-direction shader: a vertical gradient between a
// horizon and zenith palette chosen by sun height, a sun disc with a tight and
// a wide halo, and animated FBM clouds whose coverage threshold drifts so
// cloud groups form, move and dissipate. A storm value darkens and desaturates
// the palette, drops the cloud threshold and turns on the rain overlay.
//
// Rain is a second fullscreen pass anchored to the world view direction (so
// turning your head reveals different rain rather than dragging it along),
// with three layers of slanted streak cells.
#pragma once

#include <string>

#include "common.h"

namespace kaj {

class Assets;

enum class WeatherPreset { Clear, Fair, Overcast, Storm, Night, Cycle };

// The bands the rolling weather picks between, in worsening order.
enum class WeatherBand { Clear, Fair, Overcast, Storm, Count };

class Sky {
 public:
  // One full sunrise-to-sunrise takes exactly this long, and the weather is
  // re-rolled this often too, so the sky is on a single steady ten-minute
  // clock rather than drifting to its own schedule.
  static constexpr float kDayCycleSeconds = 600.0f;
  // Six independent rolls per day: one every hundred seconds.
  static constexpr int kWeatherRolls = 6;
  static constexpr float kWeatherPhaseSeconds =
      kDayCycleSeconds / kWeatherRolls;

  bool Init();
  void Shutdown();

  void Update(float dt, Assets& assets, Vector3 listener);
  void DrawSky(const Camera3D& cam, int screenW, int screenH);
  // `indoors` fades the rain out when there is a roof overhead.
  void DrawRain(const Camera3D& cam, int screenW, int screenH);
  void SetShelter(float t) { shelter_ += (Clampf(t, 0.0f, 1.0f) - shelter_) * 0.15f; }
  // Full-screen white-out from a nearby strike; drawn over everything.
  void DrawLightningFlash(int screenW, int screenH);

  void SetPreset(WeatherPreset p);
  WeatherPreset preset() const { return preset_; }
  const char* presetName() const;
  // While cycling, the band currently in force; otherwise the band the fixed
  // preset corresponds to. This is what the HUD should show -- "CYCLE" tells
  // you nothing about whether it is about to rain.
  const char* bandName() const;
  WeatherBand band() const { return band_; }
  // Seconds until the next weather roll, and how far through the day we are
  // (0 = midnight, 0.5 = noon). Both only mean anything while cycling.
  float secondsToNextRoll() const { return phaseTimer_; }

  // Drives the world fog so geometry matches the sky.
  Color horizonColor() const;
  Color fogColor() const;
  // Light the world is bathed in: bright and neutral at noon, warm and low at
  // dusk, cold and dim at night, flattened and grey in a storm.
  Color ambientColor() const;
  float fogScale() const;   // storms pull the fog in

  Vector3 sunDir() const { return sunDir_; }
  float storm() const { return storm_; }
  float rain() const { return rain_; }
  float lightning() const { return lightning_; }
  Vector3 wind() const;
  float timeOfDay() const { return timeOfDay_; }
  void SetTimeOfDay(float t) { timeOfDay_ = Clampf(t, 0.0f, 1.0f); }
  void NudgeTimeOfDay(float d) { timeOfDay_ = fmodf(timeOfDay_ + d + 1.0f, 1.0f); }

 private:
  void UpdatePalette();
  // Picks the next weather band by weighted chance and sets the storm target
  // it implies. Never repeats the same band twice running -- a roll that came
  // up the same is re-rolled once, so "the weather changed" stays true.
  void RollWeather();

  Shader sky_{};
  Shader rainShader_{};
  bool ready_ = false;

  int locInvVP_ = -1, locSunDir_ = -1, locHorizon_ = -1, locZenith_ = -1;
  int locStorm_ = -1, locCloudWind_ = -1, locLightning_ = -1, locTime_ = -1;
  int rLocInvVP_ = -1, rLocTime_ = -1, rLocIntensity_ = -1, rLocHeavy_ = -1,
      rLocWind_ = -1;

  // Cycling is the normal state of the sky; the fixed presets exist for the
  // dev key, the --weather flag and the screenshot tests.
  WeatherPreset preset_ = WeatherPreset::Cycle;
  WeatherBand band_ = WeatherBand::Fair;
  float stormTarget_ = 0.15f;
  float storm_ = 0.15f;
  float rain_ = 0.0f;
  float timeOfDay_ = 0.42f;      // 0 = midnight, 0.5 = noon
  float dayRate_ = 1.0f / kDayCycleSeconds;
  float phaseTimer_ = kWeatherPhaseSeconds;

  float shelter_ = 0.0f;   // 1 = fully under cover, rain suppressed
  float lightning_ = 0.0f;
  float nextStrike_ = 6.0f;
  float thunderDelay_ = -1.0f;
  float thunderVolume_ = 0.0f;

  float windAngle_ = 0.7f;
  float windStrength_ = 26.0f;
  float cloudWindX_ = 0.0f, cloudWindY_ = 0.0f;
  float elapsed_ = 0.0f;

  Vector3 sunDir_{0.4f, 0.7f, 0.35f};
  Vector3 horizon_{0.62f, 0.68f, 0.76f};
  Vector3 zenith_{0.22f, 0.40f, 0.68f};
};

}  // namespace kaj
