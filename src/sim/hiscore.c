#include "hiscore.h"

#include <string.h>

void hiscore_defaults(hiscore_table_t *t)
{
    // The Hivebreaker Corps' own records.
    static const char NAMES[HISCORE_COUNT][3] = {
        { 'V', 'E', 'X' }, { 'K', 'A', 'I' }, { 'R', 'O', 'O' }, { 'J', 'E', 'T' }, { 'M', 'O', 'X' },
        { 'A', 'S', 'H' }, { 'D', 'E', 'E' }, { 'B', 'O', 'B' }, { 'S', 'K', 'Y' }, { 'Z', 'E', 'D' },
    };
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < HISCORE_COUNT; i++) {
        hiscore_entry_t *e = &t->entries[i];
        e->score = (uint32_t)(12000 - 1100 * i);
        memcpy(e->initials, NAMES[i], 3);
        e->waves = (uint8_t)(10 - i / 2);
        e->squad = (uint8_t)(1 + (i % 4));
        e->flags = i < 6 ? HISCORE_EXTRACTED : 0;
    }
}

int hiscore_rank(const hiscore_table_t *t, uint32_t score)
{
    for (int i = 0; i < HISCORE_COUNT; i++)
        if (score > t->entries[i].score)
            return i;
    return -1;
}

int hiscore_insert(hiscore_table_t *t, const hiscore_entry_t *e)
{
    int r = hiscore_rank(t, e->score);
    if (r < 0)
        return -1;
    memmove(&t->entries[r + 1], &t->entries[r], sizeof(hiscore_entry_t) * (size_t)(HISCORE_COUNT - 1 - r));
    t->entries[r] = *e;
    return r;
}

static char printable(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ? c : ' ';
}

void hiscore_sanitize(hiscore_table_t *t)
{
    for (int i = 0; i < HISCORE_COUNT; i++)
        for (int k = 0; k < 3; k++)
            t->entries[i].initials[k] = printable(t->entries[i].initials[k]);
    // Insertion sort, highest first (stable).
    for (int i = 1; i < HISCORE_COUNT; i++) {
        hiscore_entry_t e = t->entries[i];
        int j = i - 1;
        while (j >= 0 && t->entries[j].score < e.score) {
            t->entries[j + 1] = t->entries[j];
            j--;
        }
        t->entries[j + 1] = e;
    }
}
