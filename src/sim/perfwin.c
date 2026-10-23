#include "perfwin.h"

void perfwin_reset(perfwin_t *w)
{
    w->head = 0;
    w->count = 0;
}

void perfwin_push(perfwin_t *w, uint32_t sample)
{
    w->samples[w->head] = sample;
    w->head = (w->head + 1) % PERFWIN_SIZE;
    if (w->count < PERFWIN_SIZE)
        w->count++;
}

uint32_t perfwin_last(const perfwin_t *w)
{
    if (!w->count)
        return 0;
    return w->samples[(w->head + PERFWIN_SIZE - 1) % PERFWIN_SIZE];
}

uint32_t perfwin_avg(const perfwin_t *w)
{
    if (!w->count)
        return 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < w->count; i++)
        sum += w->samples[i];
    return (uint32_t)(sum / w->count);
}

uint32_t perfwin_max(const perfwin_t *w)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < w->count; i++)
        if (w->samples[i] > m)
            m = w->samples[i];
    return m;
}

uint32_t perfwin_min(const perfwin_t *w)
{
    if (!w->count)
        return 0;
    uint32_t m = UINT32_MAX;
    for (uint32_t i = 0; i < w->count; i++)
        if (w->samples[i] < m)
            m = w->samples[i];
    return m;
}
