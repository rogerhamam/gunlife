# Kaj's Shooter Game 3D

A CS:GO-style first-person shooter in C++17 on raylib, set in an urban complex,
playable single player against bots or over the network.

Textures, weapon viewmodels and the movement/shooting feel come from **Grand
Theft Jack 3D** (`GTJ3D.gmk`). The sky, weather, explosion stack and the
ballistic sound design are modelled on **Naval Command**, whose SFX library is
layered on top of GTJ3D's without replacing any of it.

    play.bat           run          -> main menu
    play.bat --sp      straight into single player
    play.bat --story   straight into the wave campaign
    build.bat          compile      -> bin\kaj_shooter.exe

**A clone is playable as it stands.** `bin\kaj_shooter.exe` is committed, and
every texture, sound, model, map and music track it loads is in `assets/`.
Nothing the game opens at runtime lives outside this folder. raylib is in
`vendor/`, so `build.bat` works from a clean checkout too — you need CMake,
Ninja and a MinGW-w64 toolchain, and nothing else.

The two things *not* in the repo are the sources the asset pipeline reads
from: `GTJ3D.gmk` itself and the original 130 MB audio download. You only need
those to re-run the staging tools, and only if you want to change what is
staged. See *Assets*.

---

## Playing

| Input | Action |
|---|---|
| `W` `A` `S` `D` | move / strafe |
| Mouse | look (unrestricted 360 yaw, pitch to ±89) |
| Left click | fire — registers on the frame you click, no tick delay |
| Right click | scope (sniper) |
| Scroll wheel | cycle weapons (skips anything with no ammo) |
| `1`–`0` | select a weapon directly |
| `Space` | jump |
| `Ctrl` / `C` | crouch — the camera sinks over ~0.2 s rather than snapping |
| `Shift` | walk quietly |
| `R` | reload |
| `Tab` | scoreboard |
| `M` | toggle the GTJ3D soundtrack |
| `E` | get into / out of a vehicle |
| `Esc` | release the mouse, again for the menu |
| `F11` | fullscreen |

In a car the same keys drive it. The gunship rebinds them — see *Flying the
gunship*.

### Story mode

Menu → **STORY MODE**, or `play.bat --story`. Waves of SWAT operators that get
bigger, better shots and better armed, joined from wave three by SWAT vans,
from wave five by tanks and from wave seven by gunships. Every fifth wave is a
**heavy assault**: more of everything and tougher enemies.

    wave        1     3     5     7     10        15        20
    infantry    4     7    11    14    24 (heavy) 32 (heavy) 38
    at once     3     4     5     6     9          9          9
    vans        -     1     1     1     2          2          3
    tanks       -     -     2     2     3          4          5
    gunships    -     -     -     1     2          3          3
    accuracy   4.2°  3.7°  3.1°  2.5°  1.6°       1.1°       1.1°

Only so many enemies fit on the map at once, so the rest of a wave arrives as
reinforcements when their predecessors go down — a wave of twenty-four is
fought a squad at a time. Three lives for the whole run; it does not end, so
how far you get is the score.

It is played on whatever map the MAP row is set to, except that the Dev Test
Range — a flat plain with a gun line on it, and the default — is swapped for
the Urban Complex, which is a city to fight through.

The enemy armour drives itself through the same handling model you get.

* A **tank** closes to about 700 units, tracks you with its turret and shells
  you, and will not fire through a building.
* A **gunship** holds a slow orbit at around 300 units up and works you over
  with the minigun in bursts of a dozen, and puts a rocket in on every third
  pass. You can see and hear all of it: the minigun had no tracer and no muzzle
  flash at all, so one working you over from four hundred units up was
  invisible except for the damage, and its rotor only ever looped for the
  machine *you* were sitting in, so a hostile one overhead was silent until it
  opened fire. Both now carry from a long way off.
* A **SWAT van** is `obj_car`'s swat block: it drives at you carrying a squad
  and, once it is inside 340 units and has come to a stop, empties eight
  operators into the street out of the back doors, fanning both sides. GTJ3D
  stopped at 180 units with `irandom_range(4, 6)` aboard; this one stands off
  a little further so the squad has room to spread rather than spawning on top
  of you. An emptied van stops counting as a hostile — it becomes an ordinary
  parked vehicle, which you are welcome to steal.

All three take damage from anything you can throw at them — small arms scratch
armour at a sixth rate, rockets and grenades do the job properly.

The operators themselves are one side and cannot hurt each other, by rifle or
by blast. Left on the deathmatch rule of "shoot whoever is nearest", a squad
arriving together gunned itself down in the street before it reached anybody.
About **one in five** carries a launcher — rolled once when the slot is filled,
so a squad is mostly rifles with the odd rocket coming in rather than a firing
line of launchers. The rest make do with grenades. Both are rate-limited per
operator and have a minimum range: a bot that fires a launcher into somebody's
chest takes the blast itself, and its own explosives are the only ones that can
still hurt it.

The van has no gun of any kind — it never sets `firedWeapon`, so there is
nothing for it to fire. Everything it brings to the fight walks out of the back
and shoots on its own two feet.

`--storywave <n>` starts partway up the curve, which is how the screenshot
tests reach the armour without playing six waves first.

### Single player

Menu → **SINGLE PLAYER**, or `play.bat --sp`. It hosts a match on an ephemeral
loopback port (so it can never clash with a real server or let anyone in),
spawns bots, grabs the mouse and opens the dev overlay. Same code path as a
network game — bots occupy real player slots — so anything you test here
behaves identically online.

### Dev keys

| Key | Action |
|---|---|
| `F1` | show/hide this key list in game |
| `F3` | stats overlay (position, speed, particles, weather, ping) |
| `F5` | noclip |
| `F6` | god mode |
| `F7` | cycle weather: clear → fair → overcast → storm → night → cycle |
| `F8` | hold to advance the time of day |
| `F9` | detonate a test blast 140 units ahead |
| `F10` | refill all ammo |

### Hosting and joining

    play.bat --host --bots 4
    play.bat --connect 192.168.1.20
    play.bat --sp --weather storm

Flags: `--sp`, `--story`, `--storywave <n>`, `--host`, `--connect <ip>`,
`--port <n>`, `--name <n>`, `--bots <n>`,
`--weather clear|fair|overcast|storm|night|cycle`, `--width`, `--height`,
`--fullscreen`. Default port is UDP **27015**.

---

## Weapons

Fire rates are GTJ3D's `gun_reload(n)` cooldowns in 60 Hz ticks. Damage is
scaled up from GTJ's numbers so a 100 HP deathmatch plays like CS; head shots
do ×4 and leg shots ×0.75.

Every cone of fire in the game runs through `kSpreadScale`, currently **0.80**
— a flat 20 % tightening applied everywhere spread is used: the player's
weapons including the airborne penalty, the crouch bonus and the scoped figure,
the bots' rifles, the aim they lead grenades and rockets with, the tank's roof
gun and both miniguns. It lives in one place rather than being baked into
twenty numbers, so the table below still reads as the cone each weapon was
*authored* with and the tuning stays one auditable knob.

| # | Weapon | Mode | Damage | Cooldown | Mag | Notes |
|---|---|---|---|---|---|---|
| 1 | Fists | melee | 40 | 30t | — | 95 from behind |
| 2 | Pistol | semi | 26 | 20t | 12 | GTJ's handgun cadence |
| 3 | SMG | auto | 17 | 4t | 30 | GTJ's 900 RPM assault rifle rate |
| 4 | Rifle | auto | 24 | 6t | 30 | 600 RPM |
| 5 | Shotgun | semi | 8 × 13 | 50t | 8 | shell-by-shell reload |
| 6 | Sniper | semi | 115 | 90t | 5 | rifle report pitched to 0.55 and played loud, 300t reload, 18° scope |
| 7 | Rocket Launcher | projectile | 120 blast | 120t | 1 | speed 8/tick, radius 110 |
| 8 | Grenade | projectile | 100 blast | 80t | ×4 | speed 6 +2.5 lift, gravity 0.2 |
| 9 | Proximity Mine | placed | 115 blast | 60t | ×3 | arms after 1 s, 46 u trigger |
| 0 | Smoke Grenade | projectile | none | 70t | ×3 | pops, then 15 s of screen; blinds the AI |

The semi-auto marksman rifle was removed; the sniper covers that role. Semi-auto
*fire* is still how the pistol, shotgun and sniper work. The tripflare was
removed too, and the smoke grenade took its slot.

### Smoke

Thrown like a frag, a little harder and flatter, because you want it landing
where you are *going* rather than at your feet. It pops rather than detonating
— a small report and a burst, no fireball, no shockwave, no damage at all —
and then the canister sits where it stopped and vents for fifteen seconds.
It is drawn the whole time it is venting, so you can see where the cloud is
coming from and roughly how much of it is left.

The cloud is fed continuously rather than dropped in one puff at pop time: a
single puff gives a static ball that hangs there and then blinks out, whereas
a steady feed of plumes thrown in every direction billows outward and keeps
moving. It goes hard for the first three quarters of a second as the canister
blows off, holds, and tails away over the last four seconds. Emission runs on
a fixed 30 Hz cadence rather than per frame, so the cloud is the same density
at 30 fps as at 500.

**The AI genuinely cannot see through it.** A bot picking a target tests line
of sight against the world *and* against the smoke, so a canister between you
and a squad breaks their lock as surely as a wall does — and because they
re-target every dozen ticks, stepping into it loses you. What the AI tests is
not the particle cloud, which is a client-side thing: it is a sphere round the
canister that blooms over the first second and thins over the last three, so
both ends of the connection agree on where the smoke is.

Rockets and grenades detonate on contact, as GTJ3D's `obj_bullet` and
`obj_grenade` did. The grenade detonation uses `snd_explosion` — the sound
`obj_explosion_effect` assigned to `control.play_sound` in the GMK — and the
projectile wears GTJ3D's own `tex_grenade` on a modelled body with a lever.
Your own mines and tripflares ignore you.

### Shooting details

* **Tracers and muzzle flash sit on the barrel.** Each weapon's muzzle was
  measured from where the flash actually lights up in its sprite sheet, stored
  as a point in GTJ3D's 640×480 HUD space, and unprojected into the world each
  frame. The flash and every tracer therefore start exactly at that gun's own
  barrel tip and spear out to wherever the round lands — including misses.
  Every shotgun pellet gets its own tracer from the same muzzle, and the muzzle
  bobs with the viewmodel.
* **The muzzle flash is GTJ3D's, plus light.** The flame painted into the
  viewmodel frames is kept, and the game layers an instant yellow-to-red fire
  burst (the fire palette ramps through exactly that in the few frames the
  particles live), a fan of petals, barrel smoke, and — the part that sells it
  — a **real point light** that floods the geometry around the muzzle for a
  few hundredths of a second. Explosions light the world the same way. The
  sniper and shotgun belch two-and-a-half times the smoke, and the rocket
  launcher adds a backblast out of the rear of the tube.
  (`tools/strip_muzzle_fire.py` can erase the painted flame if you ever want
  the light and smoke on their own; it no longer runs as part of staging.)
* **Sniper and SMG use imported artwork.** `tools/import_viewmodel.py` keys the
  vignette out of a rendered weapon image, crops to the subject and scales it
  to draw 1:1. The background cannot be colour-keyed — it runs from near-black
  at the top to warm orange at the bottom, and that orange is the skin tone of
  the arms — so the tool turns the artwork's outlines into barriers using a
  luminance gradient and floods the background inward from the top and sides
  only. (Seeding the bottom row lands inside the arms, which run off the frame
  there, and hollows them out.)
* **The pistol does not animate when fired.** Its sheet kicks the whole gun up
  the screen, which read as a distracting bounce. `fireAnimTicks = 0` holds the
  idle frame.
* **Procedural recoil.** `vmRecoil` gives every weapon a sharp kick back down
  the screen with a touch of rise and roll, easing out over ~0.16 s. The
  imported sniper and SMG are single frames, so this is their only motion;
  sheets that animate their own recoil carry a small value or none.
* **Rounds travel.** Tracers are a short streak moving from muzzle to impact at
  the weapon's muzzle velocity (4000–9500 u/s), and they dim with distance so a
  round cracking past a hundred metres away is a faint scratch. Bullet audio is
  tied to the same flight: the fly-by whoosh fires when the round actually
  passes your ear, and if it buries itself in a wall first you hear the impact
  instead and never hear the whoosh.
* **Bullet holes.** Every strike on the world leaves a lasting mark — a dark
  pit with a chipped rim, randomly rolled so repeated hits do not stamp an
  identical sprite. They hold full opacity for most of a 90-second life.
  Note that decals are drawn with backface culling **off**: a decal is a single
  quad whose winding follows whichever way its surface normal points, so with
  culling on the marks vanished from every wall that came out clockwise and
  survived only at corners, where the adjacent face saved them.

  **A hole hugs what it is on.** Two things do that. The standoff that keeps a
  decal out of its wall's depth-test tolerance is applied at *draw* time and
  scaled by camera distance — a fraction of a unit up close, opening up only
  far enough away that nobody can see the gap. That emulates `glPolygonOffset`,
  which rlgl does not expose; baking a fixed 1.2 units in meant a hole visibly
  hovered proud of the brickwork as you walked up to it. And the mark follows
  the round in: the travel direction is projected onto the face, and the hole
  is oriented down that line and stretched by how shallow the impact was, up to
  three and a half times as long as it is wide. A square hit still gets the
  random roll it always had, so repeated shots do not stamp an identical
  sprite.
* **Firing animations are per weapon, not per cooldown.** GTJ3D stretched the
  animation across the whole reload delay, which left the sniper holding its
  muzzle flash for a second and a half. Each weapon now names its own animation
  length, so the sniper flashes exactly as fast as the rifle and then sits idle.
* **Reload animations and audio.** The gun swings down out of frame and tilts
  while the magazine is out, shakes gently through the middle of the reload,
  then swings back up with a small overshoot. GTJ3D only ever shipped one
  reload clip — `snd_reload_shotgun` — and that plays exactly once per reload:
  once per shell for the shotgun, once per magazine for everything else (the
  rocket tube pitched down for a heavier thunk). The shotgun's pump animation
  runs across most of its cooldown so the cocking action reads properly.
  `R` reloads at any time, interrupting a shot cooldown.
* **Number keys switch instantly** — no draw delay.
* **No input lag.** Firing is evaluated on the frame the button goes down, not
  on the next 60 Hz tick.
* Cycling weapons is silent.

---

## Explosions and effects

Written from scratch — no recycled GTJ3D sprites. The particle textures are
generated procedurally at startup. Every blast is Naval Command's stack:

1. a pressure wave, thrown as a ring of fast soft dust — it used to be a
   wireframe sphere plus a segmented ring, which showed up as stray lines and
   facets cutting through the fireball
2. a bright white-hot core flash
3. an **eruption** of fire chunks and smoke shot outward in random 3D directions
4. **chaos lobes** — random sub-fireballs that break up the silhouette
5. a **bulbous smoke cloud** that blooms at near-full radius on frame one and
   lingers, with a canopy above and a skirt hugging the ground
6. **shrapnel** — fast streaks with ember trails
7. **debris** — tumbling chunks with gravity, drag and bouncing, some on fire
8. a scorch decal on the street

Particles carry buoyancy, an exponential drag factor and coherent-noise
turbulence — smoke swirls and breaks up while flashes stay crisp, exactly as in
Naval Command's `SmokeSystem::advance`. Fire draws additive, smoke alpha-blended
back-to-front, both wind-driven. Everything except the debris chunks is a
camera-facing quad, so there is no mesh silhouette to catch the light.

Note that the back-to-front sort needs a strict, total comparator: a single
non-finite particle position makes it inconsistent, which is undefined
behaviour in `std::sort` and in practice walks off the end of the range and
spins. Non-finite particles are reaped before the sort and the comparator
tie-breaks on index.

---

## Enemies, blood and dying

Enemies are a **black SWAT operator**, built procedurally from shaded boxes
rather than a sprite — trousers into boots, plate carrier over fatigues, belt
rig, shoulder pads, upper arms tucked against the ribs with slimmer forearms
angled toward the weapon, balaclava with the face left open, eyes, and a helmet
with a brim. Deliberately compact: narrow shoulders, tight limbs, everything
pulled toward the centre line so the silhouette reads as a person rather than a
stack of crates. Team colour is a shoulder flash rather than a paint job over
the whole figure. It reads identically from every angle, which a flat sprite
cannot. (`--showcase` parks a static one 46 units ahead for inspection.)

Hits throw a heavy spray out the exit side, a **back-spatter cone** thrown the
other way — the round shoving material back out of the entry wound — and a mist
hanging over both. Rockets trail burning exhaust and a smoke column along their
flight path.

**Blood lands on the scenery.** Every hit and every death throws spots off the
wound and each one is its own short raycast in its own direction, so the
splatter climbs walls, runs across ceilings and wraps corners rather than being
a decal stamped on the ground under the body. The cone follows the shot —
material leaves a wound the way the round was going — with a wide spread, a sag
toward the floor and one cast straight down for the pool. Every spot is rolled
fresh: size, colour from arterial through to nearly dry, and lifetime, so no
two hits stamp the same pattern and a patch of splatter has depth in it. It
thins with distance, so a wall right behind somebody takes a heavy spot and a
far one takes a fleck. A death paints far more of it, and an explosive death
throws it in every direction at once.

**Deaths are physical, and shaped by where the round landed.** The server
reports the direction of the killing blow and how far up the body it hit. The
figure is broken into **fourteen segments** — helmet, head, neck, two upper
arms, two forearms, chest, abdomen, pelvis, two thighs, two shins — and each is
assessed against the wound:

* Within a hand's width of the wound channel the segment is **destroyed**, and
  shatters into 2–6 fragments whose count scales with how central the hit was.
  Fragments show meat on their broken faces. A chest shot scatters far more
  than a foot shot.
* Further out the segment survives but is thrown, hard near the wound and
  barely at all at the far end of the body.
* Head shots throw the helmet and head 2.2× harder.
* Every part is a rigid box with its own spin, gravity, drag and ground bounce.
  Meat bleeds while airborne and leaves a pool where it settles.
* Weapon dictates force: rifle 120, shotgun 175, sniper 200, explosive 320.

**Explosive deaths gib everything** — every segment shatters regardless of
distance from the blast, plus viscera, extra blood sprays and a low red burst so
the moment reads at distance.

You do not see your own corpse (the camera stays at your eye position, so it
would spawn wrapped around the lens), and any body part that lands within 14
units of the lens is skipped for the same reason.

---

## Sky and weather

A fullscreen ray-direction sky shader: a vertical gradient between horizon and
zenith palettes picked by sun height (night → golden hour → day), a sun disc
with tight and wide halos, a moon opposite it, jittered point stars at night,
and animated FBM clouds whose coverage threshold drifts so cloud groups form,
move and dissipate.

### The ten-minute clock

Both the day and the weather run on one fixed ten-minute clock, and neither
drifts off it.

* **A full sunrise to sunrise takes exactly 600 seconds.**
* **The weather is re-rolled every 100 seconds** — six independent rolls per
  day — from a weighted table: clear 30 %, fair 32 %, overcast 22 %, storm
  16 %. A roll that comes up the same as the current weather is re-rolled once,
  so a change of phase is a change of weather. Each band picks a storm value
  from a range rather than a fixed number, so two overcast spells never look
  identical, and the front moves in over about twenty seconds instead of
  switching between frames.

The dev overlay shows the band in force, the countdown to the next roll and the
time of day as a clock. `F7` and `--weather` pin a fixed preset instead, which
is what the screenshot tests use; the overlay says `(pinned)` when they do.

A storm value from 0 to 1 darkens and desaturates the palette, drops the cloud
threshold, pulls the fog in to ~45 % of its clear range, drives the wind that
pushes smoke around, and switches on:

* a **rain overlay** — three layers of slanted streak cells anchored to the
  world view direction, so turning your head reveals different rain. A ray
  straight up from the eye decides whether you are under cover; step inside the
  tower and the rain eases off, step back out and it returns
* **lightning**, which floods the sky and the street, followed by **thunder**
  delayed by the strike distance

The sky also drives the world's ambient light and fog colour, so geometry always
matches what is behind it. Fog is evaluated per fragment — per vertex made the
single map-sized ground quad take the fog value of its four distant corners and
wash out completely in heavy weather.

Two things worth knowing about how this is wired. The fullscreen passes rebuild
a world ray from the fragment's NDC, and `fragTexCoord.y` runs 0 at the *top*
of the quad while NDC y is +1 at the top — miss that flip and the entire sky
renders upside down (zenith at the horizon, ground haze overhead) and the rain
appears to fall upward. And the cloud layer is a flat plane overhead, so its UV
is the ray's intersection with it, `dir.xz / dir.y`; clamping `dir.y` breaks
that projection near the horizon and makes the cloud field slide and smear as
the camera pitches, which reads as the sky being glued to the view. The layer
is faded out before the projection degenerates instead.

---

## Ballistic audio

Three consolidated pools drive everything a bullet does, each mixing the
dedicated bullet clips with Naval Command's:

| Pool | Clips | Used for |
|---|---|---|
| `nearmiss` / `passby` | `bl_passby_*` + Naval near-miss and passby | rounds cracking past your head |
| `nearhit` | `bl_dirt_*`, `bl_hitlayer_*` + Naval impacts | rounds hitting a building, dirt or any solid |
| `ricochet` | `bl_ricochet_*` | rounds skipping off metal or striking at distance |

* **Near-miss / fly-by** — tied to the round in flight, not to the trigger
  pull. Each shot's closest approach to your head is computed as a distance
  along its path; the whoosh plays when the bullet reaches that point, volume
  tapering with miss distance, and only if that point comes *before* wherever
  the round stops. Inside ~26 units you get the tighter near-miss, further out
  the passby.
* **Impact** — every strike on the world plays its impact layer as the round
  arrives; metal rings off half the time, other surfaces a quarter.
* **Bullet hit** — a distinct impact layer when a round lands on a player.
* **Dry fire and last round** — a click on empty, a warning ping on the last
  round in the magazine.
* **Rockets and grenades whoosh too.** A bullet's fly-by comes from
  `BulletTrace`, a client-side prediction of a hitscan that knows the whole
  flight in advance. A projectile is a server-simulated entity that only ever
  reports where it is *now*, so its near-miss is measured off that. Closest
  approach on its own is not enough — these things detonate on contact, so one
  aimed anywhere near you hits you or the wall behind you and the distance
  never turns around — so it fires either when something inbound gets within
  90 units, or when it passes and starts receding inside 150.

Every explosion plays exactly one report — layering two made every explosion
sound doubled — and for **rockets and grenades alike** it is GTJ3D's own
`snd_explosion`, the sound `obj_explosion_effect` assigned to
`control.play_sound`. Rockets used to reach for Naval Command's
`big_explosion`, which is an MP3: decoder padding puts a few milliseconds of
silence in front of the transient, so the bang arrived after the fireball had
already bloomed. A WAV starts on the sample it says it does, so the report
lands on the same frame as the flash. Blasts from anything else still use
`big_explosion` or the AA airburst by size.

The pistol is a Desert Eagle recording rather than GTJ3D's `snd_handgun`: a
hand cannon wants a report with some weight behind it, and GTJ's clip is a
light pop.

Jumping plays GTJ3D's `snd_jump` the instant you leave the ground, as it did in
`obj_player`.

---

## Movement

From GTJ3D's `obj_player` Step event, at a fixed 60 ticks/second:

* forward speed ramps 0.4/tick toward ±3, GameMaker friction removes 0.2/tick
* `A`/`D` apply a flat 2 units of strafe
* `z += zspeed; zspeed -= 0.15`, jump impulse 2.6 (about 22 units of air)
* player capsule: radius 7, height 20, eye at 15 (12/9 crouched)
* ledges up to 6 units are stepped over automatically

Crouch interpolates over ~0.2 s in each direction instead of snapping, and the
run bob is a smoothed two-beat footfall curve — the earlier version shook hard
enough to be distracting.

**Fall damage.** GTJ3D had none — you could step off the civic tower and walk
away, which made every rooftop a free ride down. The safe limit is set from
the jump itself: 2.6 up against 0.15 of gravity tops out about 22 units and
comes back down at 2.6 a tick, so anything at or under 3.0 has to cost
nothing or your own jump would hurt you. Past that the damage goes with the
square of the impact speed — which is proportional to the height fallen — and
by about 8 a tick, a little over 200 units or five storeys, it is fatal.
Nothing plays on a landing that did no damage: a thump on every touchdown
lands a beat after `snd_jump` and reads as the jump sound firing twice.

It travels as its own message (`MSG_FALL`) rather than as a hit on yourself,
because `MSG_HIT` rejects `target == slot` outright and caps damage at what
the named weapon could do — and no weapon-shaped cap fits a five-storey drop.
Body armour is no help against the ground.

---

## Driving and flying

`E` gets you into anything you are standing beside and looking at; `Space` or
`E` gets you out. The handling is `obj_car`'s, step for step — see the header
of `src/vehicle.cpp` for the constants, which are its constants, not re-tuned
equivalents.

### Inside a car, 1:1 with the GMK

Sit in a saloon or a SWAT van and what you are inside is GTJ3D's own
`interior_model`, rebuilt primitive for primitive from `obj_car`'s create
event: the same shell as the exterior with `box_height` taken from 6 to 12,
which lifts the cabin roof and stretches the side panels, wearing **frame 1**
of the car's skin instead of frame 0. Its steering wheel is there too, on its
own quad — a 10 × 5 wall tilted back 40° on its column, one unit ahead of
`obj_car`'s origin — drawn in the world rather than as a HUD overlay, because
that is where GTJ3D drew it: look out of the side window and the wheel stays
with the car instead of following your eyes. Its three frames are indexed by
`obj_player.turning`, as they were.

The camera is `obj_player`'s: at `car.x, car.y` — the middle of the *cabin*
box, with the bonnet running on ahead of it — and at `z + height + 2` in a car
or `z + height + 15` in the van, with `height` being GTJ3D's player eye of 14.
Our chassis origin is the middle of the whole footprint, which is half a
bonnet further forward, so the seat sits `front_length / 2` back from it.

**One deliberate deviation.** GTJ3D's shell has no glass in it. The cabin
sides are solid panels from `model_height` to the roof and the screens are
solid diagonals across the same band, so reproducing it exactly seals the
driver into a painted box with the camera in the middle of it. GTJ3D got away
with that because its `V` key swung the camera 128 units out behind the car;
here you drive from the seat. Every vertex is still `obj_car`'s and the panels
are still exactly the panels it built — the four that are really windows are
drawn as glass rather than as paint, with depth writes off so the world
outside them stays alive.

The steering wheel is drawn last of all, after the glazing, at full white: no
paint tint, no glass over it, no lighting on it. It is the one piece of the
driving view that is UI rather than scenery, and the arms on it are skin — run
either through the cabin's tint and the hands change colour with the car.

The paint picks whichever of GTJ3D's seven skins is nearest, and those seven
colours are **measured off the staged textures**, not eyeballed. Guessed values
had a light grey car coming out *pink*: grey sits almost equidistant from
several of them, and being wrong by a little in RGB is being wrong by a lot to
look at.

The same shell, with `box_height` back at 6 and frame 0 of the skin, is the
stand-in when a vehicle's `.glb` is missing — which beats the hand-stacked
boxes that used to fill in.

### Flying the gunship

| Input | Action |
|---|---|
| `W` / `S` | cyclic — nose down / nose up, which is what flies it forward and back |
| `A` / `D` | bank left / right, which slides it sideways |
| `Space` | collective up — climb |
| `Ctrl` / `C` | collective down — descend |
| Mouse | look, and the nose follows your yaw |
| `E` | get out — `Space` is the collective, so it cannot also be the door |
| `1` / `2` | minigun / rocket pods |

Tipping the rotor disc is what moves a helicopter, so `W` puts the nose down
and the thrust follows the airframe. The tilt is eased rather than snapped: the
disc takes a moment to come over, and that lag is most of what makes it feel
like an aircraft rather than a camera. Hands off the collective it holds the
height it is at, except off the deck, where it lifts itself clear rather than
grinding along the ground once the rotor is up to speed.

**It could only go up and down before, for two separate reasons.** The
collective was on `W`/`S` and forward thrust was taken from *how far down you
were looking* — so the only things `W` and `S` did were climb and descend, and
flying anywhere meant staring at the floor while you did it. Underneath that,
it could not move horizontally at all: a vehicle pushes its bounding box into
the world every tick so collision and raycasts treat it as solid, and the
gunship was testing itself against its own box, failing every tick, and having
its x and z reverted. Vertical movement is not gated by that test, which is
exactly why up and down were the only things that worked. The car branch had
always cleared its own collider before testing; this one never did.

The camera is welded to the airframe: the seat offset is carried by the whole
attitude, pitch and roll included, not just the heading. With only the yaw
applied, pitching the nose over swung the cockpit through the lens while the
camera stayed put, and the fuselage appeared to swim around a floating
viewpoint. Two other things were making the view refuse to sit still:

* the heading was being applied **twice** — `VehicleSystem::Tick` assigns the
  gunship's heading straight from the mouse, and the car-camera rule that
  carries a heading change back into your yaw was still running on top of it,
  so the view turned at double rate and the airframe was always a tick behind;
* the run bob and crouch are decayed by `LocalPlayer::Tick`, which does not
  run while you are in a vehicle — so whatever they happened to be on the
  frame you got in stayed frozen into the driving camera. They are zeroed on
  entry now.

**Thrust follows the airframe, not the compass.** A rotor pushes at right
angles to its disc, so tipping the nose down drives it forward *and* down, and
pulling up climbs as it brakes; a bank slips it toward the low wing. The
earlier version took thrust along a flat heading vector and used the pitch
only to scale it, so the nose could be pointing anywhere and the machine still
slid along level ground.

**Ordnance leaves the airframe before it arms.** A vehicle puts its bounding
box into the world so collision and raycasts treat it as solid, and the wing
pylons are inside that box — so a rocket detonated on the frame it was
created, which read as the pods simply not working. The projectile is
simulated on the server and the server knows nothing about vehicles, so the
launch point is pushed out along the line of fire until it is clear of the
hull (`VehicleSystem::ClearOfHull`) instead.

The tank has the same problem for the same reason: once the turret traverses
away from the hull the bounding box grows enough to swallow the end of the
barrel, so the first thing a shell hit was the tank that fired it. Its round is
traced from clear of its own hull now too — the player's and the AI's alike.

Both gunship weapons are audible now. The minigun's burst loop only ever
tested for the *tank's* roof gun, so the gunship fired in silence; the pods
get a launch whoosh, a backblast, a last-round warning and a reload.

**The rotor.** `split_tank.py --model heli` used to cut the airframe, and it
got it badly wrong: its deck finder looks for the height at which the
cross-section *narrows*, which is right for a tank — hull, then a much smaller
turret — and exactly wrong for a helicopter, whose widest slice by far is the
rotor disc at the very top. It settled on 40 % of the model height, so the cut
ran through the middle of the cabin and everything above it — canopy, engine
deck, tail boom, fin, blades — was welded into one piece that span on the
mast, with both halves left open along the cut.

`tools/split_heli.py` does it properly: it finds the disc as the height band
with the largest horizontal span, cuts at the waist below the hub, and takes
the rotor as the connected component above that cut which actually spans the
disc — the tail fin also pokes above the cut and is not connected to the hub
up there, so a plain "everything above the plane" test cannot tell them apart.
Triangles that straddle the cut and touch the rotor are written into **both**
files: they are the sleeve of the mast, so the airframe keeps a capped mast
and the rotor a capped hub, and there is no gap in either.

---

## The map

`assets/maps/urban.map` is plain text — edit and restart, no rebuild. It is
where cars, props and interiors go later.

    size   <x> <z>
    ground <texture> <units-per-tile>
    fog    <r> <g> <b> <start> <end>
    sky    <r> <g> <b>
    box      <x> <y> <z> <sx> <sy> <sz> <texture> [tile]
    wall     <x1> <z1> <x2> <z2> <y> <height> <thickness> <texture> [tile]
    stairs   <x> <z> <sx> <sz> <y> <height> <dir> <steps> <texture>
    building  <x> <z> <sx> <sz> <y> <storeys> <storeyH> <wallTex> <floorTex>
    townhouse <x> <z> <sx> <sz> <y> <storeys> <storeyH> <wall> <roof> <floor>
    tree      <x> <z> <y> <height> [seed]
    spawn     <x> <y> <z> <yaw>

`townhouse` is `building` at domestic scale: the same enterable shell, but
with a single straight flight of stairs instead of a switchback — its storeys
are short enough that a straight run does not collide with the floor above —
and a pitched roof instead of a flat one. Brushes are axis-aligned boxes, so
the pitch is a stack of five narrowing slabs rather than a true wedge; from
anywhere you can stand it reads as a gable.

The vacant ground in the middle of the Urban Complex is now six office blocks
of three to six storeys, a low civic block, and a street of twelve town
houses. Every one of them is enterable — doors, windows, floor slabs and
stairs all the way to the roof.

### Spawns can never be inside geometry

Four of the Urban Complex's spawns used to be inside the blocks they were
meant to stand beside — you started the round sealed in a brick box. That is
now impossible twice over. `tools/make_maps.py` tests every spawn against the
footprints it has already emitted and drops any that is not clear, and
`World::ResolveSpawns` walks any surviving bad one out to open ground at load
time on a widening ring, dropping the feet onto whatever floor is at or below
the height that was asked for — so a spawn meant for the fifth storey stays on
the fifth storey. It logs each one it has to dig out. No edit to any map file
can put a player inside a wall.

`World::FindClearPoint` is the same routine, and everything else that has to
put something down — story mode's tanks, the vehicle spawner — goes through
it too.

`building` generates a whole enterable structure in one line: outer walls with
a centred window on every side of every storey (sill and header included), a
full-height doorway on the west face at ground level, a floor slab per storey,
a stairwell zig-zagging up the north-west corner, and a flat roof with a
parapet. The civic tower in the south-east is five storeys of it, with a spawn
on every floor and one on the roof.

`dir` is the ascent direction: `0`=+X `1`=-X `2`=+Z `3`=-Z. Keep
`height / steps <= 6` or players cannot walk up.

Four blocks around a crossroads: a brick row with a fire escape and a bank, a
walled warehouse with a catwalk and four entrances, a two-level parking deck
with a vehicle ramp, and a plaza under an arched tower. A crate ladder and a
catwalk bridge reach the rooftops.

Brush faces auto-correct their winding against the face normal. Without that,
a face wound the wrong way is silently culled by OpenGL and reads as a
see-through hole in a building.

`tree` grows one out of brushes: a bark trunk with a branch stub, carrying a
crown of five overlapping leaf lobes at uneven heights, radii and sizes, all
shaken about by `seed` so no two are the same shape and a row of them is not
stamped out. Concentric slabs were tried first and read as a stepped green
ziggurat — every tier's flat top and square shoulders lined up, and a stand of
them looked like a pile of crates. The crown starts well clear of head height,
so you walk under a tree rather than into it, and because it is ordinary
brushwork a tree collides, stops bullets, takes holes, sits in the fog and is
lit by muzzle flashes like everything else.

Textures from GTJ3D: `bank brick computer concrete crate crate2 door floor
grate grate2 metal metal_door metal_panel metal_panel2 pillar shelves sky stone
stone2 tech tech2 tree wall_a wall_b wall_c wall_d wood`. `bark` and `leaves`
come from **Kingdom** (DARK CROWN) — see *Assets* below.

---

## Networking model

UDP, protocol `KAJ5`, up to 12 players.

* **Movement is client-authoritative** — each client reports its own position;
  the server stores and relays it.
* **Hit registration is shooter-authoritative** — the client raycasts against
  the interpolated positions it can see and sends the damage; the server
  rejects anything above what the weapon could do or fired from implausibly far
  away. This is what makes shooting feel immediate; it also means a modified
  client could cheat, which is the right trade for a game played with friends.
* **Everything explosive is server-authoritative** — rockets, grenades, mines
  and tripflares are simulated on the server and mirrored in snapshots, with a
  line-of-sight check on blast damage.
* Health, deaths, scores and respawns are the server's.
* Snapshots at 30 Hz; remote players interpolated 80 ms behind. Events carry
  sequence numbers and are resent in a sliding window, so a dropped packet
  never loses a kill.

---

## Assets

Nothing in `assets/` is hand-drawn:

    tools/gmk_extract.py <file.gmk> extracted/    # full GMK 8.0 parser
    tools/stage_assets.py [--with-music]          # copies what the game uses
    tools/strip_muzzle_fire.py                    # optional, not run by default
    tools/import_viewmodel.py <src> <dst>         # key + crop a weapon render
    tools/import_kingdom_trees.py                 # bark + leaves from Kingdom
    tools/make_maps.py                            # regenerate the three maps
    tools/split_tank.py                           # tank -> hull + turret
    tools/split_heli.py                           # gunship -> airframe + rotor

`gmk_extract.py` dumps everything from the GMK: 60 sounds, 208 sprites, 59
backgrounds, 21 scripts, the GML of 100 objects and 4 room layouts.
`extracted/` is the reference material — the weapon table, movement constants
and viewmodel placement were all read out of `obj_gun.gml`, `obj_player.gml`
and `extracted/scripts/`.

`stage_assets.py` also pulls sound effects from Naval Command
(`~/Desktop/Naval Command/assets/sounds`) under an `nc_` prefix, and the
dedicated bullet layers from `~/Downloads` under a `bl_` prefix. It skips
either silently if the folder is not present.

### obj_car's own skins

`stage_assets.py` stages the seven car colours GTJ3D shipped — blue, red,
white, yellow, pink, lime, black — **both frames each**, into
`assets/vehicles`. Frame 0 is the outside of the shell and frame 1 the inside;
that pair, plus the taller cabin, is the whole of obj_car's interior model. A
map's paint colour is matched to whichever of the seven is nearest in RGB, and
a SWAT van is forced to black exactly as obj_car forced it when `ID = "swat"`.
`spr_steering_wheel` (three frames, indexed by `obj_player.turning`) and
`tex_wheel` come across with them.

### Trees, from Kingdom

    python tools/import_kingdom_trees.py [--from <dir>]

Kingdom (DARK CROWN) ships no tree meshes — its trunks and canopies are built
in code and skinned with two tiling textures. Those two, `bark.png` and
`leaves.png`, are the part that is actually authored art, so those are what
come across; the geometry is rebuilt by `World::AddTree` in this game's own
hard-edged style. Both are fully opaque, which is what makes them usable on
solid brushes: no alpha means no cutout shader and no sorting, so a tree is
ordinary world geometry. They are resampled 1024 → 256 to sit alongside the
game's own 128-pixel textures without being ten times their weight.

The tool defaults to `%LOCALAPPDATA%\Programs\Kingdom\assets\textures`.

### Deleting assets is permanent

Every staged file is recorded in `assets/.staged.json`. On the next run,
anything that was staged before but is now missing from `assets/` is treated as
a deliberate deletion: it is tombstoned and never copied again, however many
times the script runs. The game handles the gap by itself — a sound bank whose
clips are all gone simply goes silent, and a missing texture falls back to
plain grey rather than a magenta checker.

    python tools/stage_assets.py                     # normal staging
    python tools/stage_assets.py --restore <name>    # un-delete one file
    python tools/stage_assets.py --restore-all       # un-delete everything

Sprite origins from GameMaker are preserved, so the viewmodels sit exactly where
GTJ3D drew them: origin-anchored at (320, 224) in a 640×480 ortho space at 2×
scale, then scaled uniformly to your window.

### Music

    python tools/localise_music.py [--from <dir>] [--rate 22050]

The four GTJ3D themes ship as 44.1 kHz stereo WAV and come to about 130 MB,
which is why the game used to stream them straight out of the download folder
rather than copying them in. That worked on the machine they were downloaded
to and nowhere else: it was the one path in the whole game that reached
outside its own directory, so a copy of the project had no soundtrack at all.

This resamples them to 22.05 kHz mono — a quarter of the bytes, and behind
gunfire the difference is not audible — and writes them into `assets/music`,
which is now the only place the game looks. Pure Python on purpose: there is
no ffmpeg here and `audioop` was removed in 3.13, so the stereo mix-down and
the decimation are done by hand over an array of samples. Press `M`.

---

## Layout

    src/common.h      units, tuning constants, angle helpers
    src/assets.*      textures, sprite sheets, multi-clip sound banks, music
    src/world.*       map file parsing, brushes, collision, raycasts
    src/weapons.*     weapon table and per-player ammo state
    src/entities.h    server-simulated projectiles and placed charges
    src/fx.*          particles, debris, shrapnel, shockwaves, tracers, decals
    src/sky.*         sky shader, weather, rain, lightning, thunder
    src/player.*      networked player record + local FPS controller
    src/vehicle.*     GTJ3D's obj_car handling, the GMK shell, hostile armour
    src/story.*       the wave campaign: curve, state machine, banners
    src/net.*         UDP sockets, server (incl. bots), client
    src/render.*      fog shader, world/players/entities, viewmodel, HUD
    src/game.*        glue: frame-rate firing, 60 Hz tick, events, menu, dev tools
    src/main.cpp      window, command line

---

## Testing

    tools/smoketest.ps1    every weapon fired at a fixed spot, all four skies,
                           all three maps, every vehicle, three story waves
    tools/mptest.ps1       two processes, host + joiner, facing each other

Both write PNGs to `testshots/`. They drive the game through `--autoshot`,
`--autopos`, `--autopin`, `--autoyaw`, `--autopitch`, `--autofire`,
`--autowalk`, `--autodrive`, `--autoscores`, `--storywave` and `--weather`,
which exist purely for this.

The vehicle and story runs are the ones sensitive to how long the machine
takes to get going: the gunship run measures a pass by altitude, and it needs
a second of door-and-start and two seconds of rotor spool-up before it climbs
at all. A slow start eats into the 6.5-second deadline. If it reads a dozen
units rather than a hundred or more, run it again on its own before believing
it. `--autodrive` holds the collective as well as the cyclic when it is in a
gunship, because since the controls were rebound the throttle no longer
climbs; on the ground it does not, since `Space` is the door there.

Note: raylib refuses to write a screenshot to a path containing an apostrophe,
and this folder has one — keep screenshot paths relative to the working
directory.

`--autoshot` measures its deadline from the first frame rather than process
start, disables vsync (an unfocused window gets throttled hard by the driver),
and carries a watchdog. The game also logs a one-line startup breakdown:

    INIT: window 0.15s  audio device 0.02s
    ASSETS: load times  textures 0.10s  sprites 0.03s  sounds 0.33s  total 0.46s
    INIT: assets 0.46s  map 0.00s  shaders 0.01s  fx 0.00s  sky 0.03s

Total time to playable is around a second. If a launch ever feels much slower
than that, those numbers will say which stage actually cost the time.

---

## Known limits

* No ammo or health pickups; you get a full loadout on every respawn.
* Blood shares one 1400-decal budget with bullet holes and scorch marks,
  oldest first. The cap was 420, which was ample when nothing bled; one death
  throws up to thirty spots, so a firefight in a doorway used to start eating
  its own splatter within seconds. A long enough one still will.
* Enemies do not animate — no walk cycle, no firing pose.
* Corpses come apart on impact rather than being solved as a jointed ragdoll;
  parts are independent rigid bodies, which reads well but will occasionally
  separate further than anatomy allows.
* Bots walk at you and jump when they bump something — they will not path
  around a building to reach you. The same is true of a hostile tank: it turns
  toward you and nudges around whatever it bumps rather than routing past it.
* Weather is cosmetic: rain does not wet surfaces and lightning does not light
  geometry beyond the ambient flash.
* Story mode is single player only. The campaign runs on the client and drives
  the local server directly; a joiner would see the wave enemies but not the
  wave state.
* The protocol carries twelve player slots, so that is the hard ceiling on
  live enemies. A SWAT van arriving on a wave that is already at its
  concurrent cap will put fewer than eight operators on the ground — it logs
  how many it managed.
* The GMK car shell is only fitted to the saloon and the SWAT van, because
  those are obj_car's own two variants. The supercar, the tank and the gunship
  keep their glTF bodies, and a supercar still gets the flat HUD steering
  wheel rather than a modelled one.
