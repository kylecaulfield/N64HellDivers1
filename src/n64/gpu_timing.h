#ifndef N64_GPU_TIMING_H
#define N64_GPU_TIMING_H

// GPU timing per frame, in microseconds.
//
//  * RDP busy: read from the RDP's hardware busy counter. Exact.
//  * GPU done: time from the start of this frame's command submission until
//    the RDP finished drawing it (timestamped in the RDP-done interrupt).
//    It covers RSP + RDP work plus any queueing behind the previous frame,
//    so when it exceeds a frame period the GPU side is the bottleneck.
//
// Dedicated RSP time would need libdragon's rspq profiler (RSPQ_PROFILE),
// which does not fit in RSP memory at our libdragon pin.
//
// GPU_TIMING_NA means "not measurable here" (e.g. an emulator without the
// RDP counters, or no frame finished yet).

#include <stdint.h>

#define GPU_TIMING_NA UINT32_MAX

void     gpu_timing_init(void);
// Call right after attaching the frame's framebuffer, before drawing.
void     gpu_timing_submit_begin(void);
// Replaces rdpq_detach_show(): detaches, and when the RDP finishes the frame
// records the time and shows it.
void     gpu_timing_detach_show(void);
// Call once per frame after submitting it: samples the RDP counters.
void     gpu_timing_frame(void);
uint32_t gpu_timing_rdp_us(void);
uint32_t gpu_timing_gpu_us(void);

#endif
