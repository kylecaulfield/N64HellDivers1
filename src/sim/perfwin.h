#ifndef SIM_PERFWIN_H
#define SIM_PERFWIN_H

// Rolling window of integer samples (e.g. microseconds per frame) used by the
// debug overlay and the periodic USB stats log.

#include <stdint.h>

#define PERFWIN_SIZE 32

typedef struct {
    uint32_t samples[PERFWIN_SIZE];
    uint32_t head;  // next write index
    uint32_t count; // valid samples, saturates at PERFWIN_SIZE
} perfwin_t;

void     perfwin_reset(perfwin_t *w);
void     perfwin_push(perfwin_t *w, uint32_t sample);
uint32_t perfwin_last(const perfwin_t *w);
uint32_t perfwin_avg(const perfwin_t *w);
uint32_t perfwin_max(const perfwin_t *w);
uint32_t perfwin_min(const perfwin_t *w);

#endif
