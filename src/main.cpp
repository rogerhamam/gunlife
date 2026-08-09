// Kaj's Shooter Game 3D
//
//   kaj_shooter.exe                        -> main menu
//   kaj_shooter.exe --host --bots 4        -> start hosting immediately
//   kaj_shooter.exe --connect 192.168.1.20 -> join a friend
//
// Assets, sounds and weapon feel are lifted from Grand Theft Jack 3D.
#include <cstdio>
#include <cstring>
#include <string>

#include "common.h"
#include "game.h"
#include "sky.h"

int main(int argc, char** argv) {
  kaj::LaunchOptions opts;
  int width = 1280, height = 720;
  bool fullscreen = false;
  // Development aid: render for a while, save a PNG, exit. Used to verify the
  // game actually draws without needing a human at the keyboard.
  double autoshotAfter = 0.0;
  std::string autoshotPath;
  float autoshotYaw = 1e9f;
  bool autoshotScores = false;
  Vector3 autoshotPos{};
  bool autoshotHasPos = false;
  int autoshotFire = -1;
  float autoshotPitch = 1e9f;
  bool autoshotWalk = false;
  bool autoshotDrive = false;
  bool autoshotPin = false;
  bool autoshotShowcase = false;
  kaj::WeatherPreset weather = kaj::WeatherPreset::Fair;
  bool haveWeather = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](const char* def) -> std::string {
      return (i + 1 < argc) ? argv[++i] : def;
    };
    if (a == "--host") { opts.host = true; opts.skipMenu = true; }
    else if (a == "--sp" || a == "--singleplayer") {
      opts.singlePlayer = true;
      opts.skipMenu = true;
    }
    else if (a == "--story") {
      opts.singlePlayer = true;
      opts.story = true;
      opts.skipMenu = true;
    }
    else if (a == "--storywave") {
      opts.singlePlayer = true;
      opts.story = true;
      opts.skipMenu = true;
      opts.storyWave = std::atoi(next("1").c_str());
    }
    else if (a == "--weather") {
      const std::string w = next("fair");
      if (w == "clear") weather = kaj::WeatherPreset::Clear;
      else if (w == "overcast") weather = kaj::WeatherPreset::Overcast;
      else if (w == "storm") weather = kaj::WeatherPreset::Storm;
      else if (w == "night") weather = kaj::WeatherPreset::Night;
      else if (w == "cycle") weather = kaj::WeatherPreset::Cycle;
      else weather = kaj::WeatherPreset::Fair;
      haveWeather = true;
    }
    else if (a == "--connect") {
      opts.host = false;
      opts.skipMenu = true;
      opts.connectTo = next("127.0.0.1");
    }
    else if (a == "--port") opts.port = (uint16_t)atoi(next("27015").c_str());
    else if (a == "--name") opts.name = next("Kaj");
    else if (a == "--bots") opts.bots = atoi(next("3").c_str());
    else if (a == "--map") opts.map = next("urban");
    else if (a == "--width") width = atoi(next("1280").c_str());
    else if (a == "--height") height = atoi(next("720").c_str());
    else if (a == "--fullscreen") fullscreen = true;
    else if (a == "--autoshot") {
      autoshotAfter = atof(next("3").c_str());
      autoshotPath = next("shot.png");
    }
    else if (a == "--autoyaw") autoshotYaw = (float)atof(next("0").c_str());
    else if (a == "--autoscores") autoshotScores = true;
    else if (a == "--autofire") autoshotFire = atoi(next("-1").c_str());
    else if (a == "--autopitch") autoshotPitch = (float)atof(next("0").c_str());
    else if (a == "--autowalk") autoshotWalk = true;
    else if (a == "--autodrive") autoshotDrive = true;
    else if (a == "--autopin") autoshotPin = true;
    else if (a == "--showcase") autoshotShowcase = true;
    else if (a == "--autopos") {
      autoshotPos.x = (float)atof(next("0").c_str());
      autoshotPos.y = (float)atof(next("0").c_str());
      autoshotPos.z = (float)atof(next("0").c_str());
      autoshotHasPos = true;
    }
    else if (a == "--help" || a == "-h") {
      printf(
          "Kaj's Shooter Game 3D\n"
          "  --sp                single player: local match vs bots, dev tools\n"
          "  --story             story mode: escalating waves, armour and air\n"
          "  --host              host a match and play in it\n"
          "  --connect <ip>      join a match\n"
          "  --weather <p>       clear | fair | overcast | storm | night | cycle\n"
          "  --port <n>          UDP port (default 27015)\n"
          "  --name <n>          your player name\n"
          "  --bots <n>          bots to add when hosting (default 3)\n"
          "  --map <name>        urban | suburb | sprawl | cbd | interior |\n"
          "                      outdoors  (default urban)\n"
          "  --width/--height    window size\n"
          "  --fullscreen        start fullscreen\n");
      return 0;
    }
  }

  // Automated runs (--autoshot) skip vsync: an unfocused or occluded window
  // gets throttled hard by the driver, which starved the test harness of
  // frames and made captures land before the client had even connected.
  unsigned flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
  if (autoshotAfter <= 0.0) flags |= FLAG_VSYNC_HINT;
  SetConfigFlags(flags);
  InitWindow(width, height, "Kaj's Shooter Game 3D");
  if (fullscreen) ToggleFullscreen();
  SetExitKey(KEY_NULL);          // Escape is the in-game menu, not quit
  const double tWindow = GetTime();
  InitAudioDevice();
  const double tAudio = GetTime();
  SetTargetFPS(autoshotAfter > 0.0 ? 0 : 144);
  TraceLog(LOG_INFO, "INIT: window %.2fs  audio device %.2fs", tWindow, tAudio - tWindow);

  kaj::Game game;
  if (!game.Init(opts)) {
    CloseWindow();
    return 1;
  }

  if (autoshotYaw < 1e8f) game.SetDebugYaw(autoshotYaw);
  if (autoshotScores) game.SetDebugScores(true);
  if (autoshotHasPos) game.SetDebugTeleport(autoshotPos);
  if (autoshotFire >= 0) game.SetDebugFire(autoshotFire);
  if (autoshotPitch < 1e8f) game.SetDebugPitch(autoshotPitch);
  if (autoshotWalk) game.SetDebugWalk(true);
  if (autoshotDrive) game.SetDebugDrive(true);
  if (autoshotPin) game.SetDebugPin(true);
  if (autoshotShowcase) game.SetDebugShowcase(true);
  if (haveWeather) game.SetWeather(weather);
  if (autoshotAfter > 0.0) game.SetDebugTrace(true);

  // Measured from the first frame, not from process start: loading a few dozen
  // sound clips can take longer than the capture deadline itself, which had
  // the harness screenshotting before the client had even connected.
  const double loopStart = GetTime();

  while (!WindowShouldClose() && !game.wantsQuit()) {
    if (IsKeyPressed(KEY_F11)) ToggleFullscreen();
    game.Frame();
    const double elapsed = GetTime() - loopStart;
    if (autoshotAfter > 0.0 && elapsed >= autoshotAfter) {
      TakeScreenshot(autoshotPath.c_str());
      game.DebugReport();
      break;
    }
    // Watchdog for automated runs: never let a capture session outlive its
    // deadline by more than a minute, whatever the frame rate is doing.
    if (autoshotAfter > 0.0 && elapsed > autoshotAfter + 60.0) {
      TraceLog(LOG_WARNING, "AUTOSHOT: watchdog fired, exiting");
      break;
    }
  }

  game.Shutdown();
  CloseAudioDevice();
  CloseWindow();
  return 0;
}
