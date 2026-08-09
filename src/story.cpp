#include "story.h"

#include <algorithm>
#include <cstdio>

#include "common.h"

namespace kaj {
namespace {

constexpr float kBriefingSeconds = 5.0f;
constexpr float kClearedSeconds = 6.0f;
// The map has to read empty for this long before a wave counts as cleared.
constexpr float kClearHoldSeconds = 0.8f;

}  // namespace

StoryWave WaveSpec(int wave) {
  if (wave < 1) wave = 1;
  StoryWave w;
  w.index = wave;
  w.heavy = (wave % 5) == 0;

  // Infantry grows steadily rather than exponentially -- the difficulty comes
  // as much from armour and accuracy as from the head count.
  w.infantry = 4 + static_cast<int>((wave - 1) * 1.8f);
  // ...but only so many can be on the map at once. Eight leaves slots for the
  // player and keeps the firefight readable; the rest arrive as the first
  // ones fall.
  w.concurrent = std::min(8, 3 + wave / 2);

  // SWAT vans deliver more of them from wave three.
  if (wave >= 3) w.vans = std::min(3, 1 + (wave - 3) / 4);
  // Armour from wave five.
  if (wave >= 5) w.tanks = std::min(3, 1 + (wave - 5) / 4);
  // Air support from wave seven.
  if (wave >= 7) w.gunships = std::min(2, 1 + (wave - 7) / 6);

  // Accuracy, reaction and aggression, reaching their ceiling around wave 12.
  w.skill = Clampf((wave - 1) / 11.0f, 0.0f, 1.0f);
  // Enemies get tougher slowly, and hard-capped so a rifle magazine is always
  // enough for one of them.
  w.health = std::min(200.0f, 100.0f + (wave - 1) * 9.0f);

  if (w.heavy) {
    w.infantry += 4;
    w.concurrent = std::min(9, w.concurrent + 1);
    w.health += 25.0f;
    if (wave >= 5) w.tanks += 1;
    if (wave >= 10) w.gunships += 1;
  }
  return w;
}

void Story::BeginAt(int wave) {
  Reset();
  EnterWave(wave < 1 ? 1 : wave);
}

void Story::NotifyDeath() {
  if (phase_ == Phase::Idle || phase_ == Phase::Failed) return;
  if (--lives_ > 0) return;
  lives_ = 0;
  phase_ = Phase::Failed;
  timer_ = 0.0f;
}

void Story::Reset() {
  lives_ = kStartingLives;
  phase_ = Phase::Idle;
  wave_ = 0;
  spec_ = StoryWave{};
  timer_ = 0.0f;
  pending_ = 0;
  killed_ = 0;
  startWave_ = false;
  clearHold_ = 0.0f;
}

void Story::EnterWave(int wave) {
  wave_ = wave;
  spec_ = WaveSpec(wave);
  pending_ = spec_.infantry;
  phase_ = Phase::Briefing;
  timer_ = kBriefingSeconds;
  clearHold_ = 0.0f;
  startWave_ = false;
}

void Story::Tick(int enemiesAlive) {
  if (phase_ == Phase::Idle) return;

  switch (phase_) {
    case Phase::Briefing:
      timer_ -= kTickDt;
      if (timer_ <= 0.0f) {
        phase_ = Phase::Fighting;
        timer_ = 0.0f;
        startWave_ = true;     // Game spawns the opening squad this tick
        clearHold_ = 0.0f;
      }
      break;

    case Phase::Fighting:
      timer_ += kTickDt;
      // Cleared only when nothing is alive and nothing is queued. The hold
      // stops a one-frame gap between a kill and its replacement arriving
      // from ending the wave.
      if (enemiesAlive <= 0 && pending_ <= 0) {
        clearHold_ += kTickDt;
        if (clearHold_ >= kClearHoldSeconds) {
          phase_ = Phase::Cleared;
          timer_ = kClearedSeconds;
        }
      } else {
        clearHold_ = 0.0f;
      }
      break;

    case Phase::Cleared:
      timer_ -= kTickDt;
      if (timer_ <= 0.0f) EnterWave(wave_ + 1);
      break;

    case Phase::Failed:
      timer_ += kTickDt;
      break;

    case Phase::Idle:
      break;
  }
}

std::string Story::banner() const {
  char buf[96];
  switch (phase_) {
    case Phase::Briefing:
      snprintf(buf, sizeof(buf), "WAVE %d", wave_);
      return buf;
    case Phase::Cleared:
      snprintf(buf, sizeof(buf), "WAVE %d CLEARED", wave_);
      return buf;
    case Phase::Failed:
      return "MISSION FAILED";
    default:
      return std::string();
  }
}

std::string Story::subBanner() const {
  char buf[160];
  switch (phase_) {
    case Phase::Briefing: {
      // Say what is coming, so the player can choose their ground.
      std::string parts;
      auto add = [&](int n, const char* one, const char* many) {
        if (n <= 0) return;
        char t[48];
        snprintf(t, sizeof(t), "%d %s", n, n == 1 ? one : many);
        if (!parts.empty()) parts += ", ";
        parts += t;
      };
      add(spec_.infantry, "operator", "operators");
      add(spec_.vans, "SWAT van", "SWAT vans");
      add(spec_.tanks, "tank", "tanks");
      add(spec_.gunships, "gunship", "gunships");
      snprintf(buf, sizeof(buf), "%s%s   incoming in %.0f",
               spec_.heavy ? "HEAVY ASSAULT -- " : "", parts.c_str(),
               timer_ + 0.99f);
      return buf;
    }
    case Phase::Cleared:
      snprintf(buf, sizeof(buf), "wave %d in %.0f", wave_ + 1, timer_ + 0.99f);
      return buf;
    case Phase::Failed:
      snprintf(buf, sizeof(buf), "reached wave %d   %d killed", wave_, killed_);
      return buf;
    default:
      // Nothing to announce mid-fight; the HUD strip carries the wave, the
      // enemy count and the lives while you are in it.
      return std::string();
  }
}

}  // namespace kaj
