# Runs the game through a set of scenarios, capturing a screenshot and a state
# dump for each. Verifies rendering, weapons, collision, weather and networking
# without a human at the keyboard.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$exe = Join-Path $Root "bin\gunlife.exe"
$out = Join-Path $Root "testshots"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Set-Location $Root

function Run($name, $extra) {
  Get-Process gunlife -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
  # raylib rejects filenames containing an apostrophe, and the project folder
  # has one -- so keep the screenshot path relative to the working directory.
  $shot = "testshots/$name.png"
  $args = @("--sp", "--bots", "2", "--width", "1280", "--height", "720") +
          $extra + @("--autoshot", "6.5", $shot)
  $log = & $exe @args 2>&1 | Select-String -Pattern "DEBUG:|WARNING: ASSETS|WARNING: SKY|ERROR"
  Write-Host "=== $name ==="
  $log | ForEach-Object { Write-Host "  $_" }
}

# Weapon slots: 0 fists, 1 pistol, 2 SMG, 3 rifle, 4 shotgun, 5 super shotgun,
#               6 sniper, 7 rockets, 8 grenades, 9 mines, 10 tripflares
$spot = @("--autopos", "700", "4", "1200", "--autopin")

Run "walk"      @("--autowalk", "--autoyaw", "0", "--autopos", "700", "4", "1200")
Run "knife"     ($spot + @("--autoyaw", "0", "--autopitch", "0", "--autofire", "0"))
Run "pistol"    ($spot + @("--autoyaw", "0", "--autopitch", "0", "--autofire", "1"))
Run "rifle"     ($spot + @("--autoyaw", "0", "--autopitch", "0", "--autofire", "3"))
Run "shotgun"   ($spot + @("--autoyaw", "0", "--autopitch", "0", "--autofire", "4"))
Run "supershot" ($spot + @("--autoyaw", "0", "--autopitch", "0", "--autofire", "5"))
Run "sniper"    ($spot + @("--autoyaw", "0", "--autopitch", "-2", "--autofire", "6"))
Run "rocket"    ($spot + @("--autoyaw", "0", "--autopitch", "-4", "--autofire", "7"))
Run "grenade"   ($spot + @("--autoyaw", "0", "--autopitch", "10", "--autofire", "8"))
Run "mine"      ($spot + @("--autoyaw", "0", "--autopitch", "-35", "--autofire", "9"))
Run "tripflare" @("--autopos", "1000", "4", "1200", "--autopin", "--autoyaw", "0",
                  "--autopitch", "0", "--autofire", "10")

Run "sky_clear"    ($spot + @("--autoyaw", "20", "--autopitch", "16", "--weather", "clear"))
Run "sky_overcast" ($spot + @("--autoyaw", "20", "--autopitch", "16", "--weather", "overcast"))
Run "sky_storm"    ($spot + @("--autoyaw", "20", "--autopitch", "16", "--weather", "storm"))
Run "sky_night"    ($spot + @("--autoyaw", "20", "--autopitch", "16", "--weather", "night"))

# Every shipped map must load, spawn the player and render.
foreach ($mp in @("devtest", "urban", "gtj")) {
  Run "map_$mp" @("--map", $mp, "--autoyaw", "30", "--autopitch", "0")
}

# Vehicles: walk into the nearest one, get in, and hold the throttle down. A
# pass shows y at seat height and a non-zero speed in the state dump.
Run "drive" @("--map", "devtest", "--autopos", "700", "4", "1280",
              "--autoyaw", "180", "--autodrive")
# The tank: get in, drive, and put a burst of machine-gun fire into a wall.
Run "tank" @("--map", "devtest", "--autopos", "760", "4", "2120",
             "--autoyaw", "180", "--autodrive", "--autofire", "1")
# The gunship: get in, spool up, lift off. A pass shows a few hundred units of
# altitude in the state dump.
Run "heli" @("--map", "devtest", "--autopos", "820", "4", "2560",
             "--autoyaw", "180", "--autodrive")


# Story mode. Wave 1 catches the briefing banner and the wave strip; wave 8 is
# the first with tanks, gunships and vans all on the board at once, and wave 15
# is a heavy assault at the top of the difficulty curve. A pass shows the wave
# strip in the shot and a "STORY: wave N armour" line in the log.
Run "story_wave1"  @("--storywave", "1")
Run "story_wave8"  @("--storywave", "8", "--autopitch", "10")
Run "story_wave15" @("--storywave", "15", "--autopitch", "10")

Get-Process gunlife -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "shots written to $out"
