# HIVEBREAKERS

Top-down co-op wave shooter for the Nintendo 64 (1-4 players, one screen).
Drop onto a hostile planet, complete the objective while the swarm attacks,
call in support with directional codes, then hold out until extraction.

Original homebrew built with [libdragon](https://github.com/DragonMinded/libdragon)
(preview) and [Tiny3D](https://github.com/HailToDodongo/tiny3d). No libultra or
other Nintendo SDK code. Runs on real hardware: NTSC N64 + Expansion Pak, from
an EverDrive 64 X7.

**Status: milestone M7**: three kinds of mission and five difficulty
levels. Each mission is one of **hold the zones**, **destroy the nests**
(bug nests that send out the swarm until they are blown apart) or **upload
the data** (enter the terminal's code, then hold its ring while each stage
uploads). The level, picked on the briefing, sets the waves, how hard bugs
hit, the reinforcements, how many zones, nests or stages there are, and a
score multiplier; waves also scale with the squad's size. Bots got smarter:
they dodge blows, drop sentries into the swarm, call resupplies, keep their
drops off teammates and get out from under incoming strikes.

M6 added the whole game around the missions. A loading
screen at boot, a splash, the title over a live attract demo, a menu,
options, credits and a high score table; a mission briefing that doubles as
the loading and join screen; then the mission, its results, initials for a
new high score, and the table. High scores and settings are kept in the
cartridge's EEPROM. Original music (title, mission, a faster track for the
extraction hold, victory and defeat stings) and 22 sound effects, all
synthesized by `tools/genaudio.py`. Graphics polish: blob shadows under
everything, a ground with tufts and pebbles, smoke trails behind pods and
the dropship, engine glow, the camera swooping in with the drop, fades
between screens, and a drawn logo and heading font.

Built on M5's 1-4 players on one screen (START on any controller to join,
a colour per player on their trooper, aim dots, HUD panel and results row,
a control scheme each, bots on the ports nobody plays), M4's mission loop
(pod drop, hold two zones, Extraction, a 75 s hold, boarding, results), M3's
calldowns entered as directional codes, friendly fire on everything and
reinforcements from a shared pool, and M2's swarm: waves of all three bug
types, a shared flow field, a uniform grid, skinned bugs sharing pose slots,
and the stress bench whose hardware numbers set the enemy cap (currently 64,
one constant: `SIM_ENEMY_CAP_DEFAULT` in `src/sim/tuning.h`).

## Pinned dependencies

| Dependency | Version | Where |
|---|---|---|
| libdragon | `preview` @ `39d0d6096130836a65710da6730290701d060272` (2026-09-15) | `vendor/libdragon` (submodule) |
| Tiny3D | `main` @ `f2eccc3998967f1915be58d52f1c5bb6d5598a8c` (2026-10-22) | `vendor/tiny3d` (submodule) |
| MIPS toolchain | GCC 16.2.0, binutils 2.45, newlib 4.4.0.20231231 | built from the pinned libdragon's `tools/build-toolchain.sh` |
| UNFLoader | flags checked against `3821703b` (2026-05-16) | your machine (deploy only) |

To move a pin: `git -C vendor/libdragon checkout <commit>`, commit the
submodule change, update this table. The next `make` rebuilds the SDK.

Known upstream issues at these pins (all handled, recheck when moving a pin):

- libdragon's `magma.h` and `rdpq.h` trigger warnings under Tiny3D's
  `-Wextra -Wformat-signedness -Werror`. The build treats libdragon's headers
  as system headers (`-isystem`) for Tiny3D and for our code, so warnings in
  our own code still fail the build.
- Building libdragon with its rspq profiler (`RSPQ_PROFILE=1`) overflows RSP
  memory in several microcodes (`rsp_rdpq`, the audio mixer, ...), so there
  is no RSP-only timing; the overlay reports RDP busy time and GPU frame
  completion time instead.
- `vi_wait_vblank()` waits for the scanline the VI interrupt fires on. When
  the interrupt handlers (libdragon's display callback, with its lazy FPU
  save, plus ours) take longer than that one line, it misses the vblank, and
  it can keep missing every one: until M4 this showed up as occasional 50 ms
  frames, and one M4 build hung outright. Which happens depends on cache
  layout. The main loop never calls it; it spins until the VI interrupt's
  own counter changes (`wait_vblank` in `src/n64/main.c`).

## Build

The build uses a **native** toolchain (no Docker), compiled from source by the
pinned libdragon's own script, so the compiler always matches the libdragon
commit. It installs into `.toolchain/inst` inside the repo.

One-time setup:

```sh
git clone --recursive <repo-url> && cd <repo>
# Linux / WSL: sudo apt install build-essential texinfo bison flex zlib1g-dev wget python3
# macOS: install Homebrew; the toolchain script installs the rest
make toolchain          # 30-60 minutes
```

Then, every time, **one command**:

```sh
make                    # -> hivebreakers.z64
```

`make` builds libdragon and Tiny3D from the submodules into `build/sdk` (only
when a pin changes), generates and converts the models, music, sound effects
and 2D art, and links the ROM.
If you already have a libdragon toolchain, skip `make toolchain` and pass
`N64_GCCPREFIX=/path/to/toolchain` (the directory containing
`bin/mips64-elf-gcc`).

Other targets:

| Command | What it does |
|---|---|
| `make test` | Build and run the host unit tests (needs only a host C compiler) |
| `make AUTOTEST=1` | `hivebreakers-autotest.z64`: controller 1 is replaced by a scripted bot that goes through the splash, title, menu and briefing (stepping the level to STANDARD), then moves, fires with both schemes, dives, switches weapons, uses the debug menu, pauses, calls every calldown by code, picks up the machine gun and a resupply, dies (emergency reinforce), turns bots on and has one reinforce another, switches schemes on the pause screen, "plugs in" controller 2 to join by pod and fight and controller 4 to take over a bot and unplug again, then plays a quick mission with player 2 (both zones, Extraction, the hold, boarding), checks the results screen, enters initials for the high score, checks they reached the save, then plays a nests mission and an upload mission with bots on every port, and logs `AUTOTEST PASS/FAIL` (used to smoke-test ROMs in ares without a controller) |
| `make BENCH=1` | `hivebreakers-bench.z64`: runs the stress bench by itself 2 s after boot (see below) |
| `make clean` | Remove game build output (keeps the SDK) |
| `make distclean` | Also remove the built SDK |

CI (GitHub Actions) runs the tests and builds all three ROMs on every push;
the `.z64` files are attached to each run as an artifact.

## Deploy to the EverDrive 64 X7

Needs [UNFLoader](https://github.com/buu342/N64-UNFLoader) on the machine the
cart is plugged into.

1. Plug the EverDrive's USB port into your computer.
2. Turn the console **on** and leave it in the EverDrive menu.
3. Run **one command**:

```sh
tools/deploy.sh [hivebreakers.z64]                               # macOS / Linux
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1        # Windows
```

The script uploads the ROM (`UNFLoader -f 3 -r <rom>`), the game boots, and
the USB debug log streams to the terminal and to `logs/usb-<timestamp>.log`
(`-d <file> -b`). Ctrl+C to quit. Set `UNFLOADER=/path/to/UNFLoader` if it is
not on your PATH.

**Saves.** The ROM header asks for a 4 Kbit EEPROM (`N64_ROM_SAVETYPE` in
`rom.mk`), which the EverDrive picks up by itself. To keep high scores
between power cycles, copy the ROM to the SD card and start it from the
EverDrive menu: the X7 writes the save to the SD card when you go back to
its menu (RESET), so do that before switching off. Without an EEPROM the
title says `NO EEPROM: SCORES WON'T BE KEPT` and the game runs on defaults.

## Screens

| Screen | What happens |
|---|---|
| Loading | At boot, while models, music and sounds load (a bar fills) |
| Splash | Credits card, 3 s (A, B or START skips) |
| Title | The logo over the attract demo: bots playing a real mission in the background. Shows the top score. Left alone for 30 s it shows the high score table for 10 s, then comes back; the demo starts a new mission when the last one ends, or every 2 minutes |
| Menu | **Deploy**, **High scores**, **Options**, **Credits**. Stick or D-pad, A selects, B goes back |
| Options | Music and sound volume (0-10, left/right), bots fill empty slots (on/off), erase high scores (hold A for 1.5 s), back. Saved to the EEPROM when you leave |
| Briefing | The loading screen for the mission: a generated operation name and planet, the objective (picked at random) and the steps after it, the reinforcements and score multiplier, the squad and a tip. Player 1 picks the **level** with left/right (remembered; a new save starts on RECON). Controllers 2-4 press START to join (B to leave again); player 1 presses START (or A) to drop, B to go back to the title |
| Mission | The game. START pauses (see Controls) |
| Results | After the dropship leaves (or the squad is lost): the mission's numbers, each player's row, and the score |
| Initials | For a score that makes the table: up/down picks a letter, A or right moves on, B or left goes back, START (or A on the last letter) saves it |
| High scores | The ten best missions: initials, score, level, objective, waves, squad size, and `*` if the dropship got away. After a mission, START goes to the next briefing with the same squad, B to the title |

Music: the title track plays from the title to the briefing, the mission
track from the drop, a faster one for the extraction hold, then a victory or
defeat sting on the results. It is turned down while paused.

## Players

Player 1 is always in. Anyone else presses **START** on their controller to
join: at the start of a mission they ride a pod down with the squad;
mid-mission their pod lands next to the squad (it doesn't use the
reinforcement pool, and it crushes bugs, and teammates, it lands on). With
bots on, joining takes over that port's bot where it stands. Once the
dropship is down, or on the results screen, a new player starts with the
next mission. Unplugging a controller for a second takes that player out (a
bot carries on with their trooper if bots are on). Everyone who is playing
stays in for the next mission.

**Bots** (Options, or the debug menu) play the ports 2-4 nobody has joined:
they follow player 1 and shoot the nearest bug (a bulwark's armored front
last), dive away from a blow being wound up, back away from bugs that get
close (sliding along the edge rather than into a corner), call Reinforce for
dead teammates, drop a sentry into a swarm, call a resupply when nearly out
of ammo and walk to it, shoot nests and enter the terminal's code, keep
whatever they call down clear of the squad, step out from under incoming
drops and strikes, and board the dropship. They get scheme B's aim assist.
On the title screen's demo a bot leads: it works through the objective and
calls Extraction itself.

Each port has a colour: **P1 yellow, P2 blue, P3 green, P4 pink**: the
trooper's shoulder pads, chest stripe and pack, their aim dots, their HUD
panel label and their results row. The bottom of the screen has a panel per
port: label (`AI` for a bot), health bar (a thin bar under it while
reloading), and weapon and ammo, or `DROPPING`, `DOWN`, `ABOARD`, `NEXT
MISSION`, or `PUSH START` for a plugged-in controller nobody is playing. The
shared camera frames everyone; past its widest zoom, players can't walk
further apart.

## Controls

Each player picks their scheme on the pause screen (**A** switches it;
default A; remembered in the EEPROM).

| | Scheme A | Scheme B |
|---|---|---|
| Move | Stick | Stick |
| Aim | Stick (you face where you walk) | C-buttons, 8 directions (two adjacent = diagonal) |
| Fire | Hold **Z** (facing locks, so you can strafe) | Hold a C-button |
| Dive | **A** | **Z** |
| Reload / switch weapon | Tap **B** / hold **B** | Tap **B** / hold **B** |
| Calldowns | Hold **R**: the C-buttons enter a code (no firing); release **R** to throw | same |

Both schemes have a soft aim-assist cone (about 10 degrees for A, which aims
precisely with the stick; about 23 degrees for B, half an 8-way sector).
Firing on an empty magazine reloads automatically. **Start** (from anyone
playing) pauses and shows each player's scheme and the controls; there,
player 1 **holds B** for a second to quit to the title. Debug combos all
start with **L**, which neither scheme uses:

| Combo | Does |
|---|---|
| L + D-Up | Stats overlay on/off |
| L + D-Down | Debug menu (pauses) |

In the debug menu, D-Up/D-Down pick, A activates, D-Left/D-Right change a
value, B closes:

| Item | Does |
|---|---|
| Waves | Wave spawner on/off |
| Spawn N mixed bugs | Spawns 10, 25, 50 or 100 (D-Left/Right) of the wave mix around the camera |
| Bots on free ports | Bots play ports 2-4 that nobody has joined (see Players) |
| Force max zoom | Camera fully zoomed out, the worst case for drawing |
| God mode / Infinite ammo / Refill ammo / Kill all bugs | What they say |
| No calldown cooldowns | Throw calldowns back to back |
| Kill P1 (test reinforce) | Kills player 1 even in god mode, and closes the menu |
| Enemy cap | Live bug limit, in steps of 8 |
| Flow budget | Flow-field cells expanded per tick (256-4096) |
| Quick mission | Zone capture, upload stages and the extraction hold take 1/5 of the time (6 s, 4 s and 15 s); nests that come after it have 1/5 of their health |
| Complete the objective | Holds every zone, destroys every nest or uploads every stage, so Extraction can be called; closes the menu |
| Run stress bench | The bench below (72 s); closes the menu |
| Stats overlay | Same as L + D-Up |

## Calldowns

Hold **R** and your calldown panel opens (P1 top left, P2 top right, P3 and
P4 below them, so several can be open at once): every calldown, its code
(C-Up `^`, C-Down `v`, C-Left `<`, C-Right `>`), and whether it is ready. The
inputs you have entered light up green and codes that no longer match dim. A
wrong input clears the code; keep holding R and start again. Release R on a
complete code to throw a beacon about 6 m in the direction you face (a rock
stops it short). After the call-in delay the calldown arrives on the beacon,
which blinks faster as it gets close and shows the seconds left.

| Calldown | Code | Cooldown | Call-in | What arrives |
|---|---|---|---|---|
| Reinforce | `< > ^ ^` | 10 s | 3 s | Pod with every dead teammate, one reinforcement each. Only when someone is down, one at a time |
| Resupply | `v v < >` | 60 s | 4 s | Crate: walk into it to refill all ammo, 4 uses |
| Machine Gun | `< v < ^ >` | 120 s | 4 s | Case with the support weapon (100-round belt, takes the third weapon slot) |
| Airstrike | `> ^ > v` | 45 s | 3 s | 5 bombs in a line along your throw |
| Orbital Barrage | `> v v < ^ >` | 90 s | 5 s | 15 shells at random within 8 m over 6 s (the area is ringed in red) |
| Sentry | `^ < v > ^` | 90 s | 4 s | Turret: shoots the nearest bug in 16 m, 200 rounds or 45 s |
| Extraction | `> > ^ v` | 10 s | 2 s | Calls the dropship (see below). Only once the objective is done, from inside the landing zone |

Cooldowns are per player and survive death. All of it is in
`src/sim/calldown.c` (codes and timings) and `src/sim/tuning.h`.

**Friendly fire is on for everything**: bullets hit any player except the
shooter, explosions hurt everyone in range, the sentry's bullets hit players
in its line of fire, and anything dropped from orbit crushes what it lands
on, teammates included. Bots hold fire when a teammate is in their line.

**Death and reinforcements**: a dead player stays down until a teammate's
Reinforce lands. The squad shares a pool of reinforcements per mission (5
at STANDARD, see Difficulty; `REINF` at the top left). If the whole squad is down, an emergency reinforce lands
after 5 s, using the pool. With the pool empty the squad is lost, and 5 s
later the mission fails.

## Mission

1. **Drop-in.** Each trooper rides a pod down (2 s, immune while riding);
   whatever is under a pod when it lands is crushed. Waves start after it.
2. **The objective**, one of the three below. Its sites sit 15-22 m from the
   drop point, each marked by a light column, and when one is off screen a
   chevron at the edge of the view points to it.
   - **Hold the zones.** Two zones, **A** (amber) and **B** (cyan), three at
     SEVERE and up (**C**, violet), ringed on the ground. A zone fills while
     at least one living trooper stands in it and no bug is inside it (30 s
     in all). Progress pauses, never drains, when bugs get in. The top line
     shows `HOLD THE ZONES   A 40%   B HELD`.
   - **Destroy the nests.** Three nests (four from HAZARD up): chitin mounds
     with a health bar over each. A nest wakes when a trooper comes within
     18 m, or when it is hurt from anywhere, and then sends out a group of
     bugs every 7 s (bigger groups on harder levels) until it is destroyed.
     Bullets do half damage to a nest, explosions (airstrikes, barrages,
     anything dropped on it) their full damage; nests are solid and stop
     bullets. The top line shows `DESTROY THE NESTS   1/3`.
   - **Upload the data.** A terminal with a ring around it. Stand in the ring,
     hold **R** and enter the terminal's code (six inputs, shown over the
     terminal and in your calldown panel in place of Extraction), then
     release R. The stage uploads for 20 s while anyone is in the ring
     (`STAY IN THE RING` when nobody is: it pauses, never drains), and waves
     come twice as fast meanwhile. Each stage has a new code; three stages
     (two at RECON, four from SEVERE up).
3. **Extraction.** With the objective done, the landing zone (white ring and
   marker) lights up. Anyone standing in it can call **Extraction**
   (`> > ^ v`).
4. **Hold out.** 75 s until the dropship lands (`DROPSHIP IN 1:02`); waves
   come three times as fast. The dropship is seen coming down for the last
   4 s.
5. **Board.** Once it is down, every living trooper who reaches the landing
   zone boards and leaves the field. The mission succeeds when no one living
   is left on the ground (the dead don't hold it up, but they don't count as
   extracted). If the squad is lost at any point, the mission fails.

The **results screen** shows time, waves, reinforcements used, kills by bug
type and how many troopers were extracted, then a row per player in their
colour: kills (killing blows, including their sentry's and their strikes'),
deaths, accuracy (their bullets that hit a bug), friendly hits (their bullets
that hit a teammate), calldowns thrown, and how it ended (`aboard`, `down`,
`left` behind alive, or `gone` from the game), the objective's parts done and
the level, and the mission's **score** with its breakdown. Accuracy counts
bullets that hit a bug or a nest. **START** goes on to the initials (for a new
high score) or the high score table. The same numbers go to the USB log once
per mission:

```
RESULT build=6c2d5aaa outcome=success objective=nests:3/3 difficulty=2 time=105s waves=7 kills=101/50/3 shots=728 hits=553 calldowns=8 deaths=2 reinforced=2 friendly_hits=96 extracted=4/4 score=8450 p1=76/1/235/186/36/6/aboard p2=16/0/173/91/60/1/aboard ai3=31/1/167/143/0/0/aboard ai4=31/0/153/133/0/1/aboard
```

(`objective=` is zones, nests or upload, then the parts done of how many;
`difficulty=` is the level, 1 for RECON to 5 for HIVEBREAKER. `kills=` is
skitters/gnashers/bulwarks; `shots`, `hits`, `calldowns`, `deaths` and
`friendly_hits` are the squad's totals. `score=` is followed by ` assisted`
when debug aids were used. Then one field per player, `pN=` for a person and
`aiN=` for a bot: kills/deaths/shots/hits/friendly_hits/calldowns/outcome.)

## Difficulty

Picked on the briefing with left/right. STANDARD is the tuning in
`src/sim/tuning.h`; the table is `difficulty_defs` in `src/sim/difficulty.c`.
The enemy cap stays the same at every level (it is what the hardware can
draw), so harder levels mean tougher mixes, faster waves and harder hits
rather than more bugs at once.

| Level | Wave size | Waves start at | Wave pace | Bug damage | Reinforcements | Zones / nests / stages | Nest group | Score |
|---|---|---|---|---|---|---|---|---|
| RECON | 70% | wave 1 | 130% (slower) | 70% | 7 | 2 / 3 / 2 | 2 | x0.75 |
| STANDARD | 100% | wave 1 | 100% | 100% | 5 | 2 / 3 / 3 | 3 | x1.00 |
| HAZARD | 120% | wave 2 | 90% | 115% | 5 | 2 / 4 / 3 | 3 | x1.30 |
| SEVERE | 140% | wave 3 | 80% | 130% | 4 | 3 / 4 / 4 | 4 | x1.70 |
| HIVEBREAKER | 170% | wave 4 | 70% | 150% | 3 | 3 / 4 / 4 | 5 | x2.20 |

Waves also scale with the squad (bots count): 70% of the wave for one
trooper, 85% for two, 100% for three and 115% for four. From HAZARD up a
nest's group sometimes includes a bulwark.

## Scoring

| | Points |
|---|---|
| Skitter / gnasher / bulwark killed | 10 / 25 / 60 |
| Zone held / nest destroyed / stage uploaded | 500 / 400 / 400 each |
| Extracted (the dropship got away) | 2000, plus 500 per trooper aboard |
| Time bonus (successful missions only) | 5 per second under 10 minutes |
| Reinforcement used | -250 each |

The sum is multiplied by the level's score multiplier. A failed mission
keeps its kills and objective points. The constants are `SCORE_*` in
`src/sim/tuning.h`; the sum is `mission_score()` in `src/sim/mission.c`.
The table keeps the ten best (a new score goes below equal ones). A mission
where god mode, infinite ammo, no calldown cooldowns, quick mission or
*Complete the objective* was used at any point still shows its score, marked
`DEBUG AIDS USED: NOT RANKED`, and isn't entered. A fresh cartridge starts
with a table of made-up scores to beat.

## Audio

All original: `tools/genaudio.py` (Python 3, standard library only)
synthesizes every instrument and sound from oscillators, noise and envelopes,
and writes the music as FastTracker II modules (D minor; title 92 BPM,
mission 132 BPM, hold 150 BPM, two short stings; 8 channels each). The build
converts them with libdragon's `audioconv64` (VADPCM-compressed samples) and
plays them with its XM player and mixer at 22050 Hz: 8 mixer channels for
music, 8 for sound effects. Sounds come from the sim's events (shots, hits,
deaths, explosions, code inputs, beacons, pickups, zones, reinforcements,
mission phases, a new wave), panned and quietened by where they happen on
screen, and capped per frame so a swarm dying under a barrage doesn't drown
everything else. Everything is loaded at boot and every mixer channel is
played once while the loading screen is up, so no audio allocates during a
mission (the heap watchdog checks).

To replace a track or a sound, drop `assets/music/<name>.xm` or
`assets/sfx/<name>.wav` in place (names in `rom.mk`); the build uses it
instead of the generated one, like the models.

## Testing in an emulator

Use a current [ares](https://ares-emu.net/) (distribution packages can be
old or ship without the N64 core) with **Settings > Options > Homebrew Mode**
enabled: it shows the debug log and adds extra hardware checks. ares proves
correctness. Its CPU and RSP timing is close enough to compare two versions of
the code (that is how M2 was tuned), but it does not emulate RDP timing (the
overlay shows `RDP n/a` there), so absolute frame rates and the enemy cap come
from hardware. Project64 and similar emulators prove nothing. ares may
report controllers on all four ports, so panels 2-4 show `PUSH START`.

## Debug overlay and USB telemetry

Hold **L** and press **D-Up** (any controller) to toggle the overlay:

```
HIVEBREAKERS <build>            build id (git describe of the commit it was built from)
FPS 29.91 frame 33.43ms max ... measured frame rate, average and worst frame time
CPU 5.80ms max 8.20ms sim ...   CPU busy time per frame (excludes waiting for vblank / a free framebuffer)
SIM us pl fl en cr              sim time per frame by phase, see below
       pr cd sp
REN us anim build sub hud       renderer CPU time: skeletons, matrices/particles, 3D submission, 2D text
DRAWN bugs 40 skeletons 12      bugs drawn after culling; skeletons animated this frame
RDP 4.20ms  GPU done 7.90ms     RDP busy (hardware counter); GPU done = submit start -> RDP finished
TICK 1234 dropped 0             sim ticks; dropped = ticks skipped because a frame ran long
ENT P1 E3 B4 FX20 CD0  WAVE 2   players in the mission, enemies, bullets, effect particles, calldowns; wave
RAM 1211/8192K heap 818K        RDRAM in use (code+data+heap) / installed; heap alone
```

Sim phases: **pl** players (and the per-tick enemy list and grid), **fl** flow
field, **en** bug AI and movement, **cr** crowd (bugs pushing apart, rock
collision), **pr** projectiles, **cd** calldowns, **sp** spawner and camera.
CPU time is exactly sim + anim + build + sub + hud. **sub** is issuing the 3D
commands and **hud** drawing the 2D text and panels; both include time
blocked on a full RSP command queue, so when they grow with the bug count
(and *GPU done* nears 33 ms) the frame is GPU-bound.

All times are averages over the last 32 frames. The target is 33.4 ms per
frame (the N64 runs 240p NTSC at 59.826 Hz, so 30 fps is really 29.91).

*GPU done* is measured from when the CPU starts submitting a frame to when
the RDP-finished interrupt fires. It includes RSP and RDP work and any wait
behind the previous frame; if it creeps past ~33 ms the GPU is the bottleneck.
A separate RSP-only number would need libdragon's rspq profiler, which does
not fit in RSP memory at the pinned libdragon commit (several microcodes,
including `rsp_rdpq`, overflow when it is enabled).

Once per second the same numbers go to the USB log as one line:

```
STATS t=90 fps=29.91 frame_avg=33429us frame_max=33436us cpu_avg=7503us cpu_max=11263us sim_avg=3271us ph=450/1075/168/539/260/36/220 ren=1675/813/397/1345 drawn=19 skel=13 rdp=4.20ms gpu_done=9.44ms drop=0 ent=p4/e20/b0/fx0/cd0 wave=7 ram=1516K/8192K heap=947K
```

(`ph=` is pl/fl/en/cr/pr/cd/sp and `ren=` is anim/build/sub/hud, in
microseconds.)

The heap is snapshotted 30 frames into each mission. If it grows after that
during the mission, the overlay shows `GREW!` in red and the log prints a
`WARN heap grew` line: gameplay code must never allocate. (The menus and
briefing may allocate; each mission starts a new baseline.)

## Stress bench (M2 go/no-go)

Run `hivebreakers-bench.z64` (or *Run stress bench* in the debug menu). The
bench ROM always plays a zones mission at STANDARD, whatever the save says,
so its numbers compare between builds. It
turns on bots for the ports nobody plays (so four troopers and four HUD
panels, the worst case), god mode, infinite ammo and max zoom, switches
waves off, then holds the live bug count at 16, 32, 48, 64, 80, 96, 112, 128,
160, 192, 224 and 256 in turn: a mixed swarm spawned on screen and topped up as
the bots kill it. Each level settles for 2 s and is measured for 4 s; the HUD
shows `BENCH 5/12: 80 BUGS`. It takes about 72 s and logs one line per level,
then `BENCH DONE`:

```
BENCH level=48 enemies=48 drawn=46 fps=29.91 frame_avg=33429 frame_max=49645 cpu_avg=14895 cpu_max=24519 sim=3750 ph=668/1034/352/981/215/33/198 anim=2766 build=1138 submit=620 hud=6618 rdp=-1 gpu_done=15855 drop=0 proj=0 fx=0
```

Times are microseconds per frame, averaged over the 4 s (`_max` is the worst
frame). `rdp`/`gpu_done` are -1 where the hardware counter is unavailable
(ares). The highest level that keeps `fps` at 29.9 with `drop=0` is the
ceiling; the enemy cap goes somewhat below it, leaving room for calldowns
and the mission. The bench ROM plays the mission music and every sound, so
its numbers include audio.

In ares (CPU and RSP timing only), M6 holds 29.9 fps up to 64-80 bugs, and
drops to about 25-27 at 96, where M5 held 29.5: the audio mixing and the
extra drawing (shadows, ground detail, trails) cost about one bug level of
headroom. The cap of 64 is unaffected; hardware decides whether it can go up.

## Project layout

```
src/sim/        pure-C simulation core: no libdragon includes, builds on the host
                controls, weapons, player, enemy, projectile, arena, camera,
                grid (spatial hash), flow (flow field), waves, bot,
                calldown (codes, beacons, strikes, turrets, pickups,
                reinforcements, explosions), mission (drop-in, zones,
                extraction, boarding, stats, score), difficulty, tuning.h,
                hiscore (the high score table)
src/n64/        thin N64 layer: boot checks, input (joining and leaving),
                main (the screen flow), frontend (the screens), ui (2D
                drawing helpers), save (EEPROM), sound, rendering, effects,
                HUD, results screen, debug menu, overlay, timing, stress
                bench, autotest bot; palette.h has the player colours
tests/          host unit tests for src/sim (make test)
tools/          toolchain setup, generators for the models (genmodels.py),
                music and sound (genaudio.py) and 2D art (genart.py), deploy
                scripts
assets/models/  drop-in model overrides (see assets/models/README.md)
assets/music/, assets/sfx/  drop-in .xm / .wav overrides (optional)
vendor/         pinned libdragon and Tiny3D submodules
rom.mk          ROM build (called by the top-level Makefile)
```

## Engineering rules

- **Sim/N64 split.** Everything gameplay lives in `src/sim` and compiles with
  a host compiler. `make test` fails if anything there includes a libdragon or
  Tiny3D header.
- **4 players everywhere.** Arrays are indexed by controller port even when
  one controller is plugged in.
- **Fixed timestep locked to video.** The sim ticks once every 2 vblanks
  (~29.91 Hz on NTSC), counted from the VI interrupt, never from a wall clock.
  At most 2 catch-up ticks per frame; extra ones are dropped and counted.
  All timers are in ticks. The loop waits on that interrupt counter, not on
  `vi_wait_vblank()` (see the known upstream issues above).
- **No allocation during gameplay.** Fixed pools for enemies, projectiles,
  particles and calldowns, allocated at load; audio loaded and its mixer
  channels warmed up at boot. The heap watchdog enforces it.
- **Floats, not doubles.** The N64 build uses `-ffast-math`; host tests
  compare floats with tolerances, never exactly.
- **One flow field, no per-bug pathfinding.** A single distance-to-nearest-
  player field (1 m cells, Dial's algorithm) is rebuilt incrementally under a
  per-tick budget, only as far as the farthest bug; bugs roll downhill on it,
  and head straight in within 4 m.
- **Uniform grid** (2.5 m cells) for every neighbour query: bullets, bug
  separation, player shoving, bot targeting.
- **Shared pose slots.** Bugs of a type share 7 skeletons (6 walk phases and
  an attack); animation cost does not grow with the bug count.
- **Explosions are cheap.** A few semi-transparent sprites (constant alpha:
  the RDP's additive blend wraps to black when it overflows, as libdragon's
  `rdpq_mode.h` warns), some opaque sparks and smoke, a one-rectangle screen
  flash and camera shake. The cores test depth but don't write it.
- **Mind the D-cache.** The N64 has 8 KB of direct-mapped data cache. Hot
  loops over many bugs work on compact, cell-ordered copies (see `crowd_t`).

## Models

`tools/genmodels.py` (Python 3, standard library only) generates the
placeholder low-poly, vertex-colored models as glTF, rigged and animated where
needed (soldier: `run`, `idle`; bugs: `walk`, `attack`); the build converts
them with Tiny3D's importer and packs them into the ROM filesystem. The
soldier's accents are white with vertex alpha 0; the renderer's combiner puts
each player's colour there, so all four share one mesh. Blob shadows are
one shared disc model (`shadow`) drawn under soldiers, falling pods and
drops, and the dropship;
the bugs carry theirs in their own mesh. To replace
one with a Blender/Fast64 export, drop `assets/models/<name>.glb` in place;
see `assets/models/README.md` for the export rules and the per-bug triangle,
vertex and bone budgets.

The logo, the heading font and the briefing's planet come from
`tools/genart.py` (also standard library only), drawn as pixel art and
converted with libdragon's `mksprite` and `mkfont`. The text font is
libdragon's built-in monogram (CC0).

## Naming

All names, factions and designs are original. Players are troopers of the
Hivebreaker Corps; the enemy is an insectoid swarm called the Gnash:
**skitters** (fast swarmers), **gnashers** (standard warriors) and
**bulwarks** (slow armored brutes).
