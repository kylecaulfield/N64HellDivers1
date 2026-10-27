#include "test.h"
#include "tickclock.h"

void test_tickclock_steady_30hz(void)
{
    tickclock_t c;
    tickclock_init(&c, 2, 2, 100);
    uint32_t vi = 100;
    uint32_t ticks = 0;
    // One second of NTSC vblanks (rounded) must give exactly half as many ticks.
    for (int i = 0; i < 60; i++) {
        vi++;
        ticks += tickclock_advance(&c, vi);
    }
    CHECK_EQ_INT(ticks, 30);
    CHECK_EQ_INT(c.total_ticks, 30);
    CHECK_EQ_INT(c.dropped_ticks, 0);
}

void test_tickclock_frame_every_two_vblanks(void)
{
    tickclock_t c;
    tickclock_init(&c, 2, 2, 0);
    for (uint32_t vi = 2; vi <= 200; vi += 2)
        CHECK_EQ_INT(tickclock_advance(&c, vi), 1);
}

void test_tickclock_no_time_no_tick(void)
{
    tickclock_t c;
    tickclock_init(&c, 2, 2, 7);
    CHECK_EQ_INT(tickclock_advance(&c, 7), 0);
    CHECK_EQ_INT(tickclock_advance(&c, 8), 0); // half a tick accumulated
    CHECK_EQ_INT(tickclock_advance(&c, 9), 1); // completes it
}

void test_tickclock_slow_frame_catches_up(void)
{
    tickclock_t c;
    tickclock_init(&c, 2, 2, 0);
    // A frame that took 3 vblanks: one tick now, the leftover half carries.
    CHECK_EQ_INT(tickclock_advance(&c, 3), 1);
    CHECK_EQ_INT(tickclock_advance(&c, 4), 1);
    // A frame that took 4 vblanks: two ticks, nothing dropped.
    CHECK_EQ_INT(tickclock_advance(&c, 8), 2);
    CHECK_EQ_INT(c.dropped_ticks, 0);
}

void test_tickclock_caps_and_counts_dropped(void)
{
    tickclock_t c;
    tickclock_init(&c, 2, 2, 0);
    // A 10-vblank hitch (e.g. a load) is 5 ticks: run 2, drop 3.
    CHECK_EQ_INT(tickclock_advance(&c, 10), 2);
    CHECK_EQ_INT(c.dropped_ticks, 3);
    CHECK_EQ_INT(c.total_ticks, 2);
    // Back to normal immediately afterwards, no backlog.
    CHECK_EQ_INT(tickclock_advance(&c, 12), 1);
}

void test_tickclock_counter_wraparound(void)
{
    tickclock_t c;
    tickclock_init(&c, 2, 2, 0xFFFFFFFEu);
    CHECK_EQ_INT(tickclock_advance(&c, 0x00000000u), 1);
    CHECK_EQ_INT(tickclock_advance(&c, 0x00000002u), 1);
    CHECK_EQ_INT(c.dropped_ticks, 0);
}

void test_tickclock_zero_params_are_sane(void)
{
    tickclock_t c;
    tickclock_init(&c, 0, 0, 0);
    CHECK_EQ_INT(c.vblanks_per_tick, 1);
    CHECK_EQ_INT(c.max_catchup, 1);
    CHECK_EQ_INT(tickclock_advance(&c, 1), 1);
}
