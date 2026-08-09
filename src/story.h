// Story mode: an escalating wave campaign for one player.
//
// This is a state machine and a difficulty curve, nothing else. It owns no
// entities: Game asks it what the current wave wants and does the spawning,
// and reports back how many enemies are still alive. Keeping it that way means
// the campaign can be reasoned about (and retuned) without touching the
// networking, and the same bot slots and vehicle system that serve deathmatch
// serve the campaign.
//
// The shape of the campaign:
//
//   waves 1-2    rifle infantry, a handful at a time
//   wave 3+      SWAT vans start rolling in with the infantry
//   wave 5+      armour: a tank per few waves, and the infantry get better
//   wave 7+      gunships overhead
//   every 5th    a heavy wave -- more of everything, tougher enemies
//
// It does not end. The enemy count, their accuracy, their health and the
// weight of armour all keep climbing, so how far you get is the score.
#pragma once

#include <string>

namespace kaj {

// What a single wave is made of. Counts are *totals* for the wave; `concurrent`
// caps how many are on the map at once, and the rest arrive as reinforcements
// when their predecessors go down -- the server only has twelve player slots,
// so a wave of twenty is fought a squad at a time.
struct StoryWave {
  int index = 1;
  int infantry = 0;
  int concurrent = 0;
  int vans = 0;
  int tanks = 0;
  int gunships = 0;
  float skill = 0.0f;      // 0..1: accuracy, reaction and aggression
  float health = 100.0f;   // per enemy
  bool heavy = false;      // every fifth wave
};

// The curve. Pure function of the wave number, so any wave can be inspected
// (and the tuning read) without running the game.
StoryWave WaveSpec(int wave);

class Story {
 public:
  enum class Phase {
    Idle,        // not a story game
    Briefing,    // wave announced, enemies about to arrive
    Fighting,
    Cleared,     // short breather, then the next wave
    Failed,      // you died
  };

  void Begin() { BeginAt(1); }
  // Starts partway up the curve. Used by the screenshot tests to reach the
  // waves with armour and air support without playing through the first six.
  void BeginAt(int wave);
  void Reset();

  // One fixed 60 Hz step. `enemiesAlive` is how many hostiles Game can still
  // see.
  void Tick(int enemiesAlive);

  // Called once on the frame the player goes down -- edge-triggered, because
  // the server respawns you on the same timer a "have you been dead long
  // enough" test would use, and the two raced. Spends a life; the run ends
  // when they are gone.
  void NotifyDeath();
  int lives() const { return lives_; }

  // True on the tick the wave's opening reinforcements should be spawned.
  bool ShouldStartWave() const { return startWave_; }
  void ClearStartWave() { startWave_ = false; }

  // How many of the wave's infantry have not yet been sent in.
  int reinforcementsLeft() const { return pending_; }
  void TakeReinforcements(int n) { pending_ -= n; if (pending_ < 0) pending_ = 0; }

  bool active() const { return phase_ != Phase::Idle; }
  Phase phase() const { return phase_; }
  int wave() const { return wave_; }
  const StoryWave& spec() const { return spec_; }
  float phaseTimer() const { return timer_; }
  int killed() const { return killed_; }
  void AddKill() { ++killed_; }

  // A line for the middle of the screen, empty when there is nothing to say.
  std::string banner() const;
  std::string subBanner() const;

 private:
  void EnterWave(int wave);

  // Three lives for the whole run. One death ending a campaign this long
  // reads as a bug rather than as difficulty.
  static constexpr int kStartingLives = 3;

  int lives_ = kStartingLives;
  Phase phase_ = Phase::Idle;
  int wave_ = 0;
  StoryWave spec_;
  float timer_ = 0.0f;
  int pending_ = 0;      // infantry not yet sent in
  int killed_ = 0;
  bool startWave_ = false;
  // A wave is only "cleared" once the map is empty *and* nothing is still
  // queued to arrive, and it has to stay empty for a moment -- a corpse takes
  // a frame or two to leave the roster and would otherwise end the wave early.
  float clearHold_ = 0.0f;
};

}  // namespace kaj
