#include "save.h"

#include <libdragon.h>
#include <string.h>

#define SETTINGS_VERSION 3

static const eepfs_entry_t FILES[] = {
    { .path = "/settings", .size = sizeof(settings_t), .checksum = true, .backup = true },
    { .path = "/scores", .size = sizeof(hiscore_table_t), .checksum = true, .backup = true },
    { .path = "/campaign", .size = sizeof(campaign_t), .checksum = true, .backup = true },
};

static settings_t settings;
static hiscore_table_t scores;
static campaign_t campaign;

// The file sizes are part of the EEPROM's signature: changing one wipes
// every save. Grow into the reserved bytes instead.
_Static_assert(sizeof(settings_t) == 32, "settings_t changes the EEPROM layout");
_Static_assert(sizeof(campaign_t) == 16, "campaign_t changes the EEPROM layout");
_Static_assert(sizeof(hiscore_table_t) == 120, "hiscore_table_t changes the EEPROM layout");
static bool available;

static void settings_defaults(void)
{
    // A new player starts on the easiest level; the briefing changes it.
    settings = (settings_t){ .music = 7, .sfx = 8, .bots = 0, .version = SETTINGS_VERSION, .difficulty = 0 };
}

static void clamp_settings(void)
{
    if (settings.music > 10) settings.music = 7;
    if (settings.sfx > 10) settings.sfx = 8;
    settings.bots = settings.bots ? 1 : 0;
    if (settings.difficulty >= DIFFICULTY_COUNT)
        settings.difficulty = 0;
    if (settings.planet >= PLANET_COUNT)
        settings.planet = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (settings.scheme[i] >= SCHEME_COUNT)
            settings.scheme[i] = SCHEME_A;
}

void save_init(void)
{
    settings_defaults();
    hiscore_defaults(&scores);
    campaign_reset(&campaign);
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
        save_write_campaign();
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
    if (eepfs_read("/campaign", &campaign, sizeof(campaign)) != EEPFS_ESUCCESS) {
        debugf("save: campaign unreadable, starting over\n");
        campaign_reset(&campaign);
    }
    campaign_sanitize(&campaign);
    debugf("save: loaded (top score %lu, planet 1 at %u%%)\n", (unsigned long)scores.entries[0].score,
           campaign.liberation[0]);
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

campaign_t *save_campaign(void)
{
    return &campaign;
}

void save_write_campaign(void)
{
    if (available && eepfs_write("/campaign", &campaign, sizeof(campaign)) != EEPFS_ESUCCESS)
        debugf("save: writing campaign failed\n");
}

void save_reset_campaign(void)
{
    campaign_reset(&campaign);
    save_write_campaign();
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
