#include "test.h"
#include "perfwin.h"

void test_perfwin_empty(void)
{
    perfwin_t w;
    perfwin_reset(&w);
    CHECK_EQ_INT(perfwin_avg(&w), 0);
    CHECK_EQ_INT(perfwin_max(&w), 0);
    CHECK_EQ_INT(perfwin_min(&w), 0);
    CHECK_EQ_INT(perfwin_last(&w), 0);
}

void test_perfwin_stats(void)
{
    perfwin_t w;
    perfwin_reset(&w);
    perfwin_push(&w, 10);
    perfwin_push(&w, 30);
    perfwin_push(&w, 20);
    CHECK_EQ_INT(perfwin_avg(&w), 20);
    CHECK_EQ_INT(perfwin_max(&w), 30);
    CHECK_EQ_INT(perfwin_min(&w), 10);
    CHECK_EQ_INT(perfwin_last(&w), 20);
}

void test_perfwin_rolls_over(void)
{
    perfwin_t w;
    perfwin_reset(&w);
    perfwin_push(&w, 1000000); // will be evicted
    for (int i = 0; i < PERFWIN_SIZE; i++)
        perfwin_push(&w, 5);
    CHECK_EQ_INT(w.count, PERFWIN_SIZE);
    CHECK_EQ_INT(perfwin_max(&w), 5);
    CHECK_EQ_INT(perfwin_avg(&w), 5);
    CHECK_EQ_INT(perfwin_last(&w), 5);
}
