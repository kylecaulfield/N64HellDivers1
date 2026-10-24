#ifndef SIM_HISCORE_H
#define SIM_HISCORE_H

// The high score table: the ten best missions, highest first. Pure data and
// logic here; the N64 layer keeps it in EEPROM.

#include <stdint.h>

#define HISCORE_COUNT 10

enum { HISCORE_EXTRACTED = 1 << 0 };  // flags

typedef struct {
    uint32_t score;
    char     initials[3];
    uint8_t  waves;
    uint8_t  squad;       // troopers who took part
    uint8_t  flags;
    uint8_t  difficulty;  // 0..DIFFICULTY_COUNT-1
    uint8_t  objective;   // objective_type_t
} hiscore_entry_t;

typedef struct {
    hiscore_entry_t entries[HISCORE_COUNT];
} hiscore_table_t;

// A starting table, so a fresh cartridge has scores to beat.
void hiscore_defaults(hiscore_table_t *t);
// Where `score` would land (0 = top), or -1 if it doesn't make the table.
// A new score goes below equal ones already there.
int hiscore_rank(const hiscore_table_t *t, uint32_t score);
// Insert, pushing the rest down. Returns the rank or -1.
int hiscore_insert(hiscore_table_t *t, const hiscore_entry_t *e);
// Repair a table read from storage: sorted, initials printable.
void hiscore_sanitize(hiscore_table_t *t);

#endif
