#ifndef N64_SAVE_H
#define N64_SAVE_H

// Settings and the high score table, kept in the cartridge's 4 Kbit EEPROM
// (libdragon's eepfs, each file checksummed with a backup copy). Without an
// EEPROM, or with a corrupt one, the game runs on defaults and says so.
//
// Writes take ~15 ms per 8-byte block, so they only happen between screens.

#include <stdbool.h>
#include <stdint.h>

#include "hiscore.h"
#include "sim.h"

typedef struct {
    uint8_t music;                    // volume 0..10
    uint8_t sfx;                      // volume 0..10
    uint8_t bots;                     // bots play the ports nobody joins
    uint8_t scheme[SIM_MAX_PLAYERS];  // control scheme per port
    uint8_t version;
} settings_t;

void save_init(void);
// False when there is no usable EEPROM (nothing will be kept).
bool save_available(void);
settings_t *save_settings(void);
hiscore_table_t *save_scores(void);
void save_write_settings(void);
void save_write_scores(void);
// Reset the high score table to its defaults (and write it).
void save_erase_scores(void);

#endif
