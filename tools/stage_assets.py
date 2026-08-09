#!/usr/bin/env python3
"""Copy the GTJ3D assets the game actually uses into assets/.

Run after tools/gmk_extract.py. Keeps the raw extraction untouched so the
mapping from GTJ3D name -> Gunlife name lives in exactly one place.

DELETIONS ARE PERMANENT. Every staged file is recorded in assets/.staged.json.
On a later run, anything that was staged before but is now missing from
assets/ is treated as deliberately deleted: it goes on a tombstone list and is
never copied again. Delete a sound or texture you do not want and it stays
gone, however many times this script is re-run. `--restore <name>` lifts a
tombstone; `--restore-all` clears the list.
"""

import json
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTRACT = os.path.join(ROOT, 'extracted')
GTJ_AUDIO = os.path.join(ROOT, 'drive-download-20260808T053533Z-1-001', 'audio')
OUT = os.path.join(ROOT, 'assets')

# Naval Command's sound library. These are *added* to the GTJ3D pool, never
# replacing it: ricochets, near-misses, fly-bys, heavier explosions and
# weather beds that GTJ3D simply did not have.
NAVAL_ROOT = os.path.join(os.path.expanduser('~'), 'Desktop', 'Naval Command')
NAVAL_SOUNDS = {
    # ballistic feedback
    'shell_ricochet_1.wav': 'nc_ricochet_1.wav',
    'shell_ricochet_2.wav': 'nc_ricochet_2.wav',
    'shell_nearmiss_1.mp3': 'nc_nearmiss_1.mp3',
    'shell_nearmiss_2.mp3': 'nc_nearmiss_2.mp3',
    'bullet_hit_1.mp3': 'nc_bullet_hit_1.mp3',
    'bullet_hit_2.mp3': 'nc_bullet_hit_2.mp3',
    'bullet_passby.mp3': 'nc_passby_0.mp3',
    'passby_1.mp3': 'nc_passby_1.mp3',
    'passby_2.mp3': 'nc_passby_2.mp3',
    'passby_3.mp3': 'nc_passby_3.mp3',
    'passby_4.mp3': 'nc_passby_4.mp3',
    'shell_impact_1.wav': 'nc_impact_1.wav',
    'shell_impact_2.wav': 'nc_impact_2.wav',
    # explosions
    'big_explosion.mp3': 'nc_big_explosion.mp3',
    'aa_expl_1.mp3': 'nc_aa_expl_1.mp3',
    'aa_expl_2.mp3': 'nc_aa_expl_2.mp3',
    'aa_expl_3.mp3': 'nc_aa_expl_3.mp3',
    # guns
    'auto_cannon_1.mp3': 'nc_cannon_1.mp3',
    'auto_cannon_2.mp3': 'nc_cannon_2.mp3',
    'auto_cannon_3.mp3': 'nc_cannon_3.mp3',
    'aa_mg.mp3': 'nc_mg.mp3',
    'mg_lastshot.mp3': 'nc_lastshot.mp3',
    'gun_reload.mp3': 'nc_reload.mp3',
    'arty_shot_distant.mp3': 'nc_distant_1.mp3',
    'ship_shot_distant.mp3': 'nc_distant_2.mp3',
    'plane_fire.mp3': 'nc_plane_fire.mp3',
    # weather / ambience
    'Sound Effects Heavy Rain and Thunder.mp3': 'nc_rain_thunder.mp3',
    'wind_loop.wav': 'nc_wind.wav',
    'water_splash_1.mp3': 'nc_splash_1.mp3',
    'water_splash_2.mp3': 'nc_splash_2.mp3',
    # aircraft fly-by bed
    'WWII plane engine sound effect.mp3': 'nc_plane_engine.mp3',
}

# Hand-picked ballistic layers dropped into ~/Downloads. These become the
# primary pools for near-miss whooshes, impacts on buildings and dirt, and
# ricochets; the Naval Command clips stay in the mix alongside them.
DOWNLOADS = os.path.join(os.path.expanduser('~'), 'Downloads')
DOWNLOAD_SOUNDS = {
    'bullet-passby (4).mp3': 'bl_passby_1.mp3',
    'bullet-passby (5).mp3': 'bl_passby_2.mp3',
    'bullet-passby (6).mp3': 'bl_passby_3.mp3',

    'bullet-dirt.mp3':       'bl_dirt_1.mp3',
    'bullet-dirt (1).mp3':   'bl_dirt_2.mp3',
    'bullet-dirt (2).mp3':   'bl_dirt_3.mp3',
    'bullet-dirt (3).mp3':   'bl_dirt_4.mp3',

    'bullet-ricochet.mp3':      'bl_ricochet_1.mp3',
    'bullet-ricochet (1).mp3':  'bl_ricochet_2.mp3',
    'bullet-ricochet (2).mp3':  'bl_ricochet_3.mp3',
    'bullet-ricochet (3).mp3':  'bl_ricochet_4.mp3',
    'bullet-ricochet (4).mp3':  'bl_ricochet_5.mp3',
    'bullet-ricochet (5).mp3':  'bl_ricochet_6.mp3',

    'apache-bullet-hit-layer.mp3':     'bl_hitlayer_1.mp3',
    'apache-bullet-hit-layer (1).mp3': 'bl_hitlayer_2.mp3',
    'apache-bullet-hit-layer (2).mp3': 'bl_hitlayer_3.mp3',
}

MANIFEST = os.path.join(OUT, '.staged.json')


def load_manifest():
    try:
        with open(MANIFEST, 'r', encoding='utf-8') as f:
            d = json.load(f)
            return set(d.get('staged', [])), set(d.get('removed', []))
    except (OSError, ValueError):
        return set(), set()


def save_manifest(staged, removed):
    os.makedirs(OUT, exist_ok=True)
    with open(MANIFEST, 'w', encoding='utf-8') as f:
        json.dump({'staged': sorted(staged), 'removed': sorted(removed)}, f,
                  indent=1)

# World textures: extracted background name -> in-game name
TEXTURES = {
    'texture_wall1': 'brick',
    'texture_wall2': 'concrete',
    'texture_wall3': 'wall_a',
    'texture_wall4': 'wall_b',
    'texture_wall5': 'wall_c',
    'texture_wall6': 'wall_d',
    'texture_floor': 'floor',
    'tex_metal': 'metal',
    'tex_wood': 'wood',
    'tex_box': 'crate',
    'tex_box2': 'crate2',
    'tex_wall_metal': 'metal_panel',
    'tex_wall_metal2': 'metal_panel2',
    'tex_wall_stone': 'stone',
    'tex_wall_stone2': 'stone2',
    'tex_wall_grate': 'grate',
    'tex_wall_grate2': 'grate2',
    'tex_door': 'door',
    'tex_metal_door': 'metal_door',
    'tex_pillar': 'pillar',
    'tex_bank': 'bank',
    'tex_shelves': 'shelves',
    'tex_wall_computer': 'computer',
    'tex_wall_tech': 'tech',
    'tex_wall_tech4': 'tech2',
    'tex_tree': 'tree',
    'bg_mountains': 'sky',
}

# Viewmodels: (sprite base name, frame count)
VIEWMODELS = [
    ('spr_fists', 7),
    ('spr_handgun', 5),
    ('spr_assault_rifle', 2),
    ('spr_shotgun', 7),
    ('spr_rocket_launcher', 16),
    ('spr_grenade', 3),
    ('spr_fists_hold', 7),
]

# Character art, wrapped onto the 3D enemy models. GTJ3D drew these as flat
# billboards; here each one is UV-banded across a low-poly humanoid so it keeps
# the original look but reads correctly from any angle.
CHARACTERS = [
    ('spr_policeman', 4),
    ('spr_civilian1', 2),
    ('gunman', 2),
    ('ladyn', 2),
    ('spr_addict', 2),
]

# obj_car's own shell skins. Frame 0 is the outside, frame 1 the *inside* --
# GTJ3D built a second model with a taller cabin and swapped to frame 1 the
# moment you got in and the camera went to the driver's seat, which is what
# makes the cabin read as a cabin rather than as the back of the bodywork.
# Both frames are needed, per colour.
CAR_SKINS = ['tex_bluecar', 'tex_redcar', 'tex_whitecar', 'tex_yellowcar',
             'tex_pinkcar', 'tex_limecar', 'tex_blackcar']

# The rest of what obj_car drew: the wheel it wrapped round four cylinders,
# and the steering wheel, whose three frames are indexed by obj_player.turning
# (0 straight, 1 left, 2 right).
CAR_PARTS = [('tex_wheel', 1), ('spr_steering_wheel', 3)]

# Effect sprites: (sprite base name, frame count)
FX = [
    ('spr_blood', 3),
    ('spr_explosion', 1),
    ('spr_smoke', 1),
    ('spr_crossheir', 1),
    ('spr_crack', 3),
    ('spr_small_bullet', 1),
    ('spr_mid_bullet', 1),
    ('spr_large_bullet', 1),
    ('spr_rocket_bullet', 1),
    ('tex_grenade', 1),
]

SOUNDS = [
    'snd_handgun', 'snd_shotgun', 'snd_small', 'snd_large',
    'snd_rocketlaunch', 'snd_explosion', 'snd_explosion2', 'snd_explosion3',
    'snd_jump', 'snd_swing', 'snd_punch', 'snd_hurt', 'snd_hurt2', 'snd_ow',
    'snd_pickup', 'snd_pickup2', 'snd_powerup', 'snd_reload_shotgun',
    'snd_lazer', 'snd_lazer2', 'snd_lazer3', 'snd_lazer_cannon',
    'snd_lazer_cannon2', 'snd_human_scream',
    'snd_monster_die', 'snd_monster_die2', 'snd_bump', 'snd_door_shut',
    'snd_electric', 'snd_electric_hum', 'snd_teleport', 'snd_alarm_bell',
    'snd_correct', 'snd_wrong', 'snd_fire', 'snd_lighter', 'snd_bite',
    'snd_cash', 'snd_buy', 'snd_crash',
]

MUSIC = ['bgm_theme1', 'bgm_theme2', 'bgm_theme3', 'bgm_theme4']


def main():
    staged_before, removed = load_manifest()

    if '--restore-all' in sys.argv:
        print('restoring %d previously deleted asset(s)' % len(removed))
        removed = set()
    for i, a in enumerate(sys.argv):
        if a == '--restore' and i + 1 < len(sys.argv):
            # Matches a full relative path, a bare filename, or a directory
            # prefix such as "weapons/".
            target = sys.argv[i + 1].replace('\\', '/')
            gone = {r for r in removed
                    if r == target
                    or os.path.basename(r) == target
                    or r.startswith(target.rstrip('/') + '/')}
            removed -= gone
            print('restoring %d file(s) matching %r' % (len(gone), target))

    # Anything staged on a previous run that is no longer on disk was deleted
    # on purpose. Tombstone it so it never comes back.
    fresh_deletions = []
    for rel in staged_before:
        if not os.path.exists(os.path.join(OUT, rel)) and rel not in removed:
            removed.add(rel)
            fresh_deletions.append(rel)
    for rel in sorted(fresh_deletions):
        print('  deleted, will not restage: %s' % rel)

    staged = set()
    counts = {'copied': 0, 'kept': 0, 'skipped': 0, 'missing': 0}

    def copy(src, rel):
        """rel is the path under assets/, e.g. 'sounds/snd_jump.wav'."""
        if rel in removed:
            counts['skipped'] += 1
            return
        dst = os.path.join(OUT, rel)
        if not os.path.exists(src):
            counts['missing'] += 1
            print('  MISSING source for %s' % rel)
            return
        staged.add(rel)
        if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src):
            counts['kept'] += 1
            return
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        counts['copied'] += 1

    for src_name, dst_name in TEXTURES.items():
        copy(os.path.join(EXTRACT, 'backgrounds', src_name + '.png'),
             'textures/%s.png' % dst_name)

    for base, frames in VIEWMODELS:
        for i in range(frames):
            copy(os.path.join(EXTRACT, 'sprites', '%s_%d.png' % (base, i)),
                 'weapons/%s_%d.png' % (base, i))


    for base in CAR_SKINS:
        for i in (0, 1):
            copy(os.path.join(EXTRACT, 'sprites', '%s_%d.png' % (base, i)),
                 'vehicles/%s_%d.png' % (base, i))
    for base, frames in CAR_PARTS:
        for i in range(frames):
            copy(os.path.join(EXTRACT, 'sprites', '%s_%d.png' % (base, i)),
                 'vehicles/%s_%d.png' % (base, i))

    for base, frames in FX:
        for i in range(frames):
            copy(os.path.join(EXTRACT, 'sprites', '%s_%d.png' % (base, i)),
                 'fx/%s_%d.png' % (base, i))

    for s in SOUNDS:
        copy(os.path.join(EXTRACT, 'sounds', s + '.wav'), 'sounds/%s.wav' % s)

    nc = os.path.join(NAVAL_ROOT, 'assets', 'sounds')
    if os.path.isdir(nc):
        for src, dst in NAVAL_SOUNDS.items():
            copy(os.path.join(nc, src), 'sounds/%s' % dst)
    else:
        print('  (Naval Command not found at %s, skipping its SFX)' % NAVAL_ROOT)

    if os.path.isdir(DOWNLOADS):
        for src, dst in DOWNLOAD_SOUNDS.items():
            copy(os.path.join(DOWNLOADS, src), 'sounds/%s' % dst)

    if '--with-music' in sys.argv:
        for m in MUSIC:
            copy(os.path.join(GTJ_AUDIO, m + '.wav'), 'music/%s.wav' % m)

    # NOTE: tools/strip_muzzle_fire.py used to run here to erase the painted
    # flame from the viewmodel frames. GTJ3D's own flash is wanted, so it no
    # longer runs -- the sprite flame and the game's light/smoke burst are
    # layered together. Run that script by hand if you ever want it stripped.

    save_manifest(staged, removed)
    print('assets: %d copied, %d already current, %d skipped (deleted), '
          '%d missing sources' %
          (counts['copied'], counts['kept'], counts['skipped'],
           counts['missing']))
    if removed:
        print('%d asset(s) are tombstoned; --restore-all brings them back'
              % len(removed))


if __name__ == '__main__':
    main()
