#ifndef N64_OVERLAY_H
#define N64_OVERLAY_H

// Debug overlay (toggle: hold L, press D-Up) and the once-per-second stats
// line sent over USB with debugf. Both show the same numbers.

#include <stdint.h>

#include "sim.h"
#include "tickclock.h"

// Text styles on the builtin debug font, registered by overlay_init and used
// by the HUD and the debug menu too.
// Zone A is shown in STYLE_TITLE's amber, zone B in STYLE_ZONE_B's cyan
// (render.c draws their markers in the same colours). STYLE_P1 + port is
// each player's colour (palette.h).
enum { STYLE_NORMAL, STYLE_WARN, STYLE_TITLE, STYLE_DIM, STYLE_GOOD, STYLE_ZONE_B, STYLE_P1 };

// The builtin debug font's line height (ascent 11 + descent 2): a text
// call's line_spacing is added to it, so pitch p is line_spacing p - 13.
#define FONT_LINE_H 13

// Sim phases timed through sim_t.mark: players (and the per-tick enemy list
// and grid), flow, enemies (AI and movement), crowd (separation, rocks),
// projectiles, calldowns, spawn (spawner, camera, counts).
#define TIMED_PHASES (SIM_PHASE_COUNT - 1)

typedef struct {
    uint32_t frame_us;                // wall time since the previous frame started
    uint32_t cpu_us;                  // CPU busy time this frame (sim + render submission)
    uint32_t sim_us;                  // part of cpu_us spent in sim ticks
    uint32_t phase_us[TIMED_PHASES];  // sim_us split by phase
    uint32_t anim_us;                 // renderer: skeleton updates
    uint32_t build_us;                // renderer: matrices and particles
    uint32_t submit_us;               // issuing the 3D commands; grows when the RSP queue
                                      // is full, i.e. when GPU-bound
    uint32_t hud_us;                  // 2D text and panels: HUD, debug menu, overlay
    uint16_t drawn_bugs;              // bugs on screen after culling
    uint16_t skeletons;               // skeletons updated
} frame_timing_t;

void overlay_init(void);
void overlay_toggle(void);
bool overlay_is_visible(void);
// Snapshot the heap usage; any growth after this point is flagged, because
// gameplay must not allocate. Call once loading is finished.
void overlay_set_heap_baseline(void);
// Stop watching (outside gameplay the screens load and free things).
void overlay_clear_heap_baseline(void);
// Record this frame's numbers; logs the stats line once per second.
void overlay_record(const frame_timing_t *t, const sim_t *sim, const tickclock_t *clock);
// Draw into the currently attached framebuffer (call after the 3D scene).
void overlay_draw(void);
// "a/b/c/..." for the per-phase microseconds, as in the STATS and BENCH lines.
void overlay_format_phases(char *buf, int size, const uint32_t us[TIMED_PHASES]);

#endif
