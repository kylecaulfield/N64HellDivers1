#include "save.h"

#include <libdragon.h>
#include <string.h>

#define SETTINGS_VERSION 1

static const eepfs_entry_t FILES[] = {
    { .path = "/settings", .size = sizeof(settings_t), .checksum = true, .backup = true },
    { .path = "/scores", .size = sizeof(hiscore_table_t), .checksum = true, .backup = true },
};

static settings_t settings;
static hiscore_table_t scores;
static bool available;

static void settings_defaults(void)
{
    settings = (settings_t){ .music = 7, .sfx = 8, .bots = 0, .version = SETTINGS_VERSION };
}

static void clamp_settings(void)
{
    if (settings.music > 10) settings.music = 7;
    if (settings.sfx > 10) settings.sfx = 8;
    settings.bots = settings.bots ? 1 : 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (settings.scheme[i] >= SCHEME_COUNT)
            settings.scheme[i] = SCHEME_A;
}

void save_init(void)
{
    settings_defaults();
    hiscore_defaults(&scores);
    eeprom_type_t type = eeprom_present();
    if (type == EEPROM_NONE) {
        debugf("save: no EEPROM, running on defaults\n");
        return;
    }
    int err = eepfs_init(FILES, sizeof(FILES) / sizeof(FILES[0]));
    if (err != EEPFS_ESUCCESS) {
        debugf("save: eepfs_init failed (%d), running on defaults\n", err);
        return;
    }
    available = true;
    if (!eepfs_verify_signature()) {
        // A blank (or someone else's) EEPROM: start ours.
        debugf("save: new EEPROM, writing defaults\n");
        eepfs_wipe();
        save_write_settings();
        save_write_scores();
        return;
    }
    if (eepfs_read("/settings", &settings, sizeof(settings)) != EEPFS_ESUCCESS ||
        settings.version != SETTINGS_VERSION) {
        debugf("save: settings unreadable, using defaults\n");
        settings_defaults();
    }
    clamp_settings();
    if (eepfs_read("/scores", &scores, sizeof(scores)) != EEPFS_ESUCCESS) {
        debugf("save: scores unreadable, using defaults\n");
        hiscore_defaults(&scores);
    }
    hiscore_sanitize(&scores);
    debugf("save: loaded (top score %lu)\n", (unsigned long)scores.entries[0].score);
}

bool save_available(void)
{
    return available;
}

settings_t *save_settings(void)
{
    return &settings;
}

hiscore_table_t *save_scores(void)
{
    return &scores;
}

void save_write_settings(void)
{
    if (available && eepfs_write("/settings", &settings, sizeof(settings)) != EEPFS_ESUCCESS)
        debugf("save: writing settings failed\n");
}

void save_write_scores(void)
{
    if (available && eepfs_write("/scores", &scores, sizeof(scores)) != EEPFS_ESUCCESS)
        debugf("save: writing scores failed\n");
}

void save_erase_scores(void)
{
    hiscore_defaults(&scores);
    save_write_scores();
}
