#include "gpu_timing.h"

#include <libdragon.h>
#include <rdp.h>

#define DP_COUNTER_MASK 0x00FFFFFF // the RDP counters are 24 bits wide
#define DP_RESET_COUNTERS (DP_WSTATUS_RESET_CLOCK_COUNTER | DP_WSTATUS_RESET_CMD_COUNTER | \
                           DP_WSTATUS_RESET_PIPE_COUNTER | DP_WSTATUS_RESET_TMEM_COUNTER)

// One slot per frame in flight (plus one spare), handed to the RDP-done callback.
#define SLOTS 4
typedef struct {
    surface_t *fb;
    uint32_t submit_ticks; // TICKS_READ() when submission started
} frame_slot_t;

static frame_slot_t slots[SLOTS];
static int cur_slot;
static volatile uint32_t gpu_done_ticks = GPU_TIMING_NA;
static uint32_t rdp_us = GPU_TIMING_NA;

void gpu_timing_init(void)
{
    *DP_STATUS = DP_RESET_COUNTERS;
}

void gpu_timing_submit_begin(void)
{
    cur_slot = (cur_slot + 1) % SLOTS;
    slots[cur_slot].submit_ticks = TICKS_READ();
}

// Runs under interrupt when the RDP has finished the frame.
static void on_frame_done(void *arg)
{
    frame_slot_t *slot = arg;
    gpu_done_ticks = (uint32_t)TICKS_DISTANCE(slot->submit_ticks, TICKS_READ());
    display_show(slot->fb);
}

void gpu_timing_detach_show(void)
{
    frame_slot_t *slot = &slots[cur_slot];
    slot->fb = (surface_t *)rdpq_get_attached();
    rdpq_detach_cb(on_frame_done, slot);
}

void gpu_timing_frame(void)
{
    // Counters run at the RCP clock and wrap after ~268 ms, plenty for a frame.
    uint32_t clock = *DP_CLOCK & DP_COUNTER_MASK;
    uint32_t busy  = *DP_BUSY & DP_COUNTER_MASK;
    *DP_STATUS = DP_RESET_COUNTERS;
    // Emulators without the counters read zero (ares runs the clock but not
    // the busy counter); real hardware drawing a frame is never 0 busy.
    rdp_us = (clock && busy) ? (uint32_t)((uint64_t)busy * 1000000u / RCP_FREQUENCY) : GPU_TIMING_NA;
}

uint32_t gpu_timing_rdp_us(void)
{
    return rdp_us;
}

uint32_t gpu_timing_gpu_us(void)
{
    uint32_t t = gpu_done_ticks;
    return t == GPU_TIMING_NA ? GPU_TIMING_NA : (uint32_t)TICKS_TO_US((uint64_t)t);
}
