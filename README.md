# HIVEBREAKERS

Top-down co-op wave shooter for the Nintendo 64 (1-4 players, one screen).
Drop onto a hostile planet, complete the objective while the swarm attacks,
call in support with directional codes, then hold out until extraction.

Original homebrew built with [libdragon](https://github.com/DragonMinded/libdragon)
(preview) and [Tiny3D](https://github.com/HailToDodongo/tiny3d). No libultra or
other Nintendo SDK code. Runs on real hardware: NTSC N64 + Expansion Pak, from
an EverDrive 64 X7.

**Status: milestone M0** (toolchain, pinned SDK, hello-world ROM with debug
overlay and USB logging, host test harness, placeholder model pipeline).

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
| `make clean` | Remove game build output (keeps the SDK) |
| `make distclean` | Also remove the built SDK |

CI (GitHub Actions) runs the tests and builds the ROM on every push; the
`.z64` is attached to each run as an artifact.

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

## Testing in an emulator

Use a current [ares](https://ares-emu.net/) (distribution packages can be
old or ship without the N64 core) with **Settings > Options > Homebrew Mode**
enabled: it shows the debug log and adds extra hardware checks. ares proves
correctness only. It does not emulate RDP timing (the overlay shows `RDP n/a`
there), so frame rates and timings from it mean nothing; performance numbers
come from hardware. Project64 and similar emulators prove nothing.

## Debug overlay and USB telemetry

Hold **L** and press **D-Up** (any controller) to toggle the overlay:

```
HIVEBREAKERS <build>            build id (git describe of the commit it was built from)
FPS 29.91 frame 33.43ms max ... measured frame rate, average and worst frame time
CPU 5.80ms max 8.20ms sim ...   CPU busy time per frame (excludes waiting for vblank / a free framebuffer)
RDP 4.20ms  GPU done 7.90ms     RDP busy (hardware counter); GPU done = submit start -> RDP finished
TICK 1234 dropped 0             sim ticks; dropped = ticks skipped because a frame ran long
ENT P1 E0 B0 FX0 CD0            players (controllers), enemies, bullets, effects, calldowns
RAM 1211/8192K heap 818K        RDRAM in use (code+data+heap) / installed; heap alone
```

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
STATS t=90 fps=29.91 frame_avg=33429us frame_max=33444us cpu_avg=5818us cpu_max=8196us sim_avg=56us rdp=4.20ms gpu_done=7.96ms drop=0 ent=p1/e0/b0/fx0/cd0 ram=1211K/8192K heap=818K
```

The heap is snapshotted after the first 60 frames. If it ever grows after
that, the overlay shows `GREW!` in red and the log prints a `WARN heap grew`
line: gameplay code must never allocate.

## Project layout

```
src/sim/        pure-C simulation core: no libdragon includes, builds on the host
src/n64/        thin N64 layer: boot checks, input, rendering, overlay, timing
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

## Models

`tools/genmodels.py` (Python 3, standard library only) generates the
placeholder low-poly, vertex-colored models as glTF; the build converts them
with Tiny3D's importer and packs them into the ROM filesystem. To replace one
with a Blender/Fast64 export, drop `assets/models/<name>.glb` in place; see
`assets/models/README.md` for the export rules.

## Naming

All names, factions and designs are original. Players are troopers of the
Hivebreaker Corps; the enemy is an insectoid swarm called the Gnash:
**skitters** (fast swarmers), **gnashers** (standard warriors) and
**bulwarks** (slow armored brutes).
