# HIVEBREAKERS

Top-down co-op wave shooter for the Nintendo 64 (1-4 players, one screen).
Drop onto a hostile planet, complete the objective while the swarm attacks,
call in support with directional codes, then hold out until extraction.

Original homebrew built with [libdragon](https://github.com/DragonMinded/libdragon)
(preview) and [Tiny3D](https://github.com/HailToDodongo/tiny3d). No libultra or
other Nintendo SDK code. Runs on real hardware: NTSC N64 + Expansion Pak, from
an EverDrive 64 X7.

**Status: milestone M2** (the swarm stress test): waves of all three bug types
breaching from the arena edges, a shared flow field steering them around
rocks, a uniform grid for neighbour queries, skinned and animated bugs sharing
pose slots, bots for players 2-4, and a stress bench that measures how many
bugs the hardware holds at 30 fps. That number sets the enemy cap (currently
64, one constant: `SIM_ENEMY_CAP_DEFAULT` in `src/sim/tuning.h`). Death
respawns you after 3 s; Reinforce replaces that in M3.

## Pinned dependencies

| Dependency | Version | Where |
|---|---|---|
| libdragon | `preview` @ `39d0d6096130836a65710da6730290701d060272` (2026-09-15) | `vendor/libdragon` (submodule) |
| Tiny3D | `main` @ `f2eccc3998967f1915be58d52f1c5bb6d5598a8c` (2026-10-22) | `vendor/tiny3d` (submodule) |
| MIPS toolchain | GCC 16.2.0, binutils 2.45, newlib 4.4.0.20231231 | built from the pinned libdragon's `tools/build-toolchain.sh` |
| UNFLoader | flags checked against `3821703b` (2026-05-16) | your machine (deploy only) |

To move a pin: `git -C vendor/libdragon checkout <commit>`, commit the
submodule change, update this table. The next `make` rebuilds the SDK.

Known upstream issues at these pins (both handled, recheck when moving a pin):

- libdragon's `magma.h` and `rdpq.h` trigger warnings under Tiny3D's
  `-Wextra -Wformat-signedness -Werror`. The build treats libdragon's headers
  as system headers (`-isystem`) for Tiny3D and for our code, so warnings in
  our own code still fail the build.
- Building libdragon with its rspq profiler (`RSPQ_PROFILE=1`) overflows RSP
  memory in several microcodes (`rsp_rdpq`, the audio mixer, ...), so there
  is no RSP-only timing; the overlay reports RDP busy time and GPU frame
  completion time instead.

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
when a pin changes), generates and converts the models, and links the ROM.
If you already have a libdragon toolchain, skip `make toolchain` and pass
`N64_GCCPREFIX=/path/to/toolchain` (the directory containing
`bin/mips64-elf-gcc`).

Other targets:

| Command | What it does |
|---|---|
| `make test` | Build and run the host unit tests (needs only a host C compiler) |
| `make AUTOTEST=1` | `hivebreakers-autotest.z64`: controller 1 is replaced by a scripted bot that moves, fires with both schemes, dives, switches weapons, uses the debug menu and pauses, then logs `AUTOTEST PASS/FAIL` (used to smoke-test ROMs in ares without a controller) |
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

## Controls

Pick a scheme per player in the debug menu (default A).

| | Scheme A | Scheme B |
|---|---|---|
| Move | Stick | Stick |
| Aim | Stick (you face where you walk) | C-buttons, 8 directions (two adjacent = diagonal) |
| Fire | Hold **Z** (facing locks, so you can strafe) | Hold a C-button |
| Dive | **A** | **Z** |
| Reload / switch weapon | Tap **B** / hold **B** | Tap **B** / hold **B** |
| Calldown codes (M3) | Hold **R**: C-buttons enter the code, no firing | same |

Both schemes have a soft aim-assist cone (about 10 degrees for A, which aims
precisely with the stick; about 23 degrees for B, half an 8-way sector).
Firing on an empty magazine reloads automatically. **Start** pauses and shows
the controls. Debug combos all start with **L**, which neither scheme uses:

| Combo | Does |
|---|---|
| L + D-Up | Stats overlay on/off |
| L + D-Down | Debug menu (pauses) |

In the debug menu, D-Up/D-Down pick, A activates, D-Left/D-Right change a
value, B closes:

| Item | Does |
|---|---|
| P1 controls | Scheme A or B for player 1 |
| Waves | Wave spawner on/off |
| Spawn N mixed bugs | Spawns 10, 25, 50 or 100 (D-Left/Right) of the wave mix around the camera |
| Bots P2-P4 | Fill ports 2-4 with bots (scheme B) that fight and follow P1 |
| Force max zoom | Camera fully zoomed out, the worst case for drawing |
| God mode / Infinite ammo / Refill ammo / Kill all bugs | What they say |
| Enemy cap | Live bug limit, in steps of 8 |
| Flow budget | Flow-field cells expanded per tick (256-4096) |
| Run stress bench | The bench below (72 s); closes the menu |
| Stats overlay | Same as L + D-Up |

## Testing in an emulator

Use a current [ares](https://ares-emu.net/) (distribution packages can be
old or ship without the N64 core) with **Settings > Options > Homebrew Mode**
enabled: it shows the debug log and adds extra hardware checks. ares proves
correctness. Its CPU and RSP timing is close enough to compare two versions of
the code (that is how M2 was tuned), but it does not emulate RDP timing (the
overlay shows `RDP n/a` there), so absolute frame rates and the enemy cap come
from hardware. Project64 and similar emulators prove nothing.

## Debug overlay and USB telemetry

Hold **L** and press **D-Up** (any controller) to toggle the overlay:

```
HIVEBREAKERS <build>            build id (git describe of the commit it was built from)
FPS 29.91 frame 33.43ms max ... measured frame rate, average and worst frame time
CPU 5.80ms max 8.20ms sim ...   CPU busy time per frame (excludes waiting for vblank / a free framebuffer)
SIM us pl fl en cr pr sp        sim time per frame by phase, see below
REN us anim build sub           renderer CPU time: skeletons, matrices/particles, command submission
DRAWN bugs 40 skeletons 12      bugs drawn after culling; skeletons animated this frame
RDP 4.20ms  GPU done 7.90ms     RDP busy (hardware counter); GPU done = submit start -> RDP finished
TICK 1234 dropped 0             sim ticks; dropped = ticks skipped because a frame ran long
ENT P1 E3 B4 FX20 CD0  WAVE 2   players in the mission, enemies, bullets, effect particles, calldowns; wave
RAM 1211/8192K heap 818K        RDRAM in use (code+data+heap) / installed; heap alone
```

Sim phases: **pl** players (and the per-tick enemy list and grid), **fl** flow
field, **en** bug AI and movement, **cr** crowd (bugs pushing apart, rock
collision), **pr** projectiles, **sp** spawner and camera. CPU time is exactly
sim + anim + build + sub. **sub** is the time spent issuing GPU commands
(3D, HUD, overlay); it balloons when the RSP's command queue is full, so a
large *sub* together with *GPU done* near 33 ms means the frame is GPU-bound.

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
STATS t=90 fps=29.91 frame_avg=33429us frame_max=33433us cpu_avg=6113us cpu_max=7972us sim_avg=2401us ph=320/330/117/719/210/239 ren=1576/811/1322 drawn=26 skel=11 rdp=4.20ms gpu_done=8.89ms drop=0 ent=p1/e26/b0/fx0/cd0 wave=3 ram=1460K/8192K heap=916K
```

(`ph=` is pl/fl/en/cr/pr/sp and `ren=` is anim/build/sub, in microseconds.)

The heap is snapshotted after the first 60 frames. If it ever grows after
that, the overlay shows `GREW!` in red and the log prints a `WARN heap grew`
line: gameplay code must never allocate.

## Stress bench (M2 go/no-go)

Run `hivebreakers-bench.z64` (or *Run stress bench* in the debug menu). It
turns on bots for players 2-4, god mode, infinite ammo and max zoom, switches
waves off, then holds the live bug count at 16, 32, 48, 64, 80, 96, 112, 128,
160, 192, 224 and 256 in turn: a mixed swarm spawned on screen and topped up as
the bots kill it. Each level settles for 2 s and is measured for 4 s; the HUD
shows `BENCH 5/12: 80 BUGS`. It takes about 72 s and logs one line per level,
then `BENCH DONE`:

```
BENCH level=96 enemies=96 drawn=95 fps=30.03 frame_avg=33303 frame_max=49762 cpu_avg=27088 cpu_max=38203 sim=5831 ph=1149/531/663/2683/272/196 anim=3035 build=2101 submit=16119 rdp=-1 gpu_done=27955 drop=0 proj=2 fx=40
```

Times are microseconds per frame, averaged over the 4 s (`_max` is the worst
frame). `rdp`/`gpu_done` are -1 where the hardware counter is unavailable
(ares). The highest level that keeps `fps` at 29.9 with `drop=0` is the
ceiling; the enemy cap goes somewhat below it, leaving room for calldowns,
audio and the objective (M3+).

## Project layout

```
src/sim/        pure-C simulation core: no libdragon includes, builds on the host
                controls, weapons, player, enemy, projectile, arena, camera,
                grid (spatial hash), flow (flow field), waves, bot, tuning.h
src/n64/        thin N64 layer: boot checks, input, rendering, effects, HUD,
                debug menu, overlay, timing, stress bench, autotest bot
tests/          host unit tests for src/sim (make test)
tools/          toolchain setup, placeholder model generator, deploy scripts
assets/models/  drop-in model overrides (see assets/models/README.md)
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
  All timers are in ticks.
- **No allocation during gameplay.** Fixed pools for enemies, projectiles,
  particles and calldowns, allocated at load. The heap watchdog enforces it.
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
- **Mind the D-cache.** The N64 has 8 KB of direct-mapped data cache. Hot
  loops over many bugs work on compact, cell-ordered copies (see `crowd_t`).

## Models

`tools/genmodels.py` (Python 3, standard library only) generates the
placeholder low-poly, vertex-colored models as glTF, rigged and animated where
needed (soldier: `run`, `idle`; bugs: `walk`, `attack`); the build converts
them with Tiny3D's importer and packs them into the ROM filesystem. To replace
one with a Blender/Fast64 export, drop `assets/models/<name>.glb` in place;
see `assets/models/README.md` for the export rules and the per-bug triangle,
vertex and bone budgets.

## Naming

All names, factions and designs are original. Players are troopers of the
Hivebreaker Corps; the enemy is an insectoid swarm called the Gnash:
**skitters** (fast swarmers), **gnashers** (standard warriors) and
**bulwarks** (slow armored brutes).
