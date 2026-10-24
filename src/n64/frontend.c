#include "frontend.h"

#include <libdragon.h>
#include <stdio.h>
#include <string.h>

#include "difficulty.h"
#include "overlay.h"
#include "palette.h"
#include "ui.h"
#include "version.h"

#define FONT FONT_BUILTIN_DEBUG_MONO

static const rdpq_textparms_t CENTRE = { .width = 320, .align = ALIGN_CENTER };

// --- Navigation -------------------------------------------------------------

nav_t frontend_nav(const input_frame_t *in, uint8_t ports)
{
    // Held directions repeat after 12 frames, then every 4.
    static int held_frames;
    static int last_dir;
    nav_t n = { .port = -1 };
    int dir = 0; // 1 up, 2 down, 3 left, 4 right
    for (int p = 0; p < SIM_MAX_PLAYERS; p++) {
        if (!(ports & (1u << p)) || !in->ports[p].connected)
            continue;
        const sim_input_t *i = &in->ports[p];
        if (i->pressed & (SIM_BTN_A | SIM_BTN_START)) {
            n.accept = true;
            n.port = p;
        }
        if (i->pressed & SIM_BTN_START)
            n.start = true;
        if (i->pressed & SIM_BTN_B) {
            n.back = true;
            n.port = p;
        }
        if (!dir) {
            if ((i->held & SIM_BTN_DUP) || i->stick_y > 50) dir = 1;
            else if ((i->held & SIM_BTN_DDOWN) || i->stick_y < -50) dir = 2;
            else if ((i->held & SIM_BTN_DLEFT) || i->stick_x < -50) dir = 3;
            else if ((i->held & SIM_BTN_DRIGHT) || i->stick_x > 50) dir = 4;
        }
    }
    bool fire = false;
    if (dir && dir == last_dir) {
        held_frames++;
        fire = held_frames >= 12 && (held_frames - 12) % 4 == 0;
    } else {
        held_frames = 0;
        fire = dir != 0;
    }
    last_dir = dir;
    if (fire) {
        n.up = dir == 1;
        n.down = dir == 2;
        n.left = dir == 3;
        n.right = dir == 4;
    }
    return n;
}

// --- Briefing ---------------------------------------------------------------

static const char *const ADJ[] = {
    "IRON", "SILENT", "CRIMSON", "HOLLOW", "BURNING", "SHATTERED", "LAST", "BITTER",
    "GRIM", "SWIFT", "STONE", "EMBER", "MIDNIGHT", "BROKEN", "DISTANT", "HUNGRY",
};
static const char *const NOUN[] = {
    "HARVEST", "LANTERN", "ANVIL", "TIDE", "SPEAR", "HAMMER", "VIGIL", "CINDER",
    "OATH", "STORM", "FURNACE", "WARDEN", "COMPASS", "BEACON", "THRESHOLD", "RECKONING",
};
static const char *const SYLLABLE[] = {
    "KA", "VEL", "DOR", "XI", "MAR", "TH", "ZEN", "OR", "UL", "QUE", "RA", "NEX", "SO", "TAR", "IO", "BRE",
};
static const char *const SUFFIX[] = { " PRIME", " IV", " VII", " MINOR", "", "-9", " II", "" };
static const char *const TIPS[] = {
    "A LANDING POD CRUSHES WHATEVER IT LANDS ON. TEAMMATES TOO.",
    "ZONES ONLY FILL WHILE NO BUG IS INSIDE THEM.",
    "BULWARKS ARE ARMORED IN FRONT. HIT THEM FROM THE SIDE.",
    "HOLD R AND ENTER A CODE WITH THE C-BUTTONS TO CALL SUPPORT.",
    "A DEAD TROOPER COMES BACK WHEN A TEAMMATE CALLS REINFORCE.",
    "THE SENTRY SHOOTS WHATEVER IS IN ITS LINE OF FIRE.",
    "DIVE (A OR Z) TO GET OUT OF A GNASHER'S REACH.",
    "THE DROPSHIP WAITS FOR EVERYONE STILL ALIVE ON THE GROUND.",
    "BULLETS ONLY SCRATCH A NEST. AIRSTRIKES AND BARRAGES DON'T.",
    "A NEST WAKES WHEN YOU COME NEAR OR SHOOT IT.",
    "THE UPLOAD PAUSES WHEN NOBODY IS INSIDE THE TERMINAL'S RING.",
    "HARDER LEVELS MULTIPLY YOUR SCORE.",
};

void briefing_make(briefing_t *b, uint32_t seed)
{
    uint32_t s = seed * 2654435761u + 1;
    s ^= s >> 13;
    snprintf(b->operation, sizeof(b->operation), "OPERATION %s %s", ADJ[s & 15], NOUN[(s >> 4) & 15]);
    int n = 2 + (int)((s >> 8) & 1);
    b->planet[0] = 0;
    for (int k = 0; k < n; k++)
        strcat(b->planet, SYLLABLE[(s >> (10 + 4 * k)) & 15]);
    strcat(b->planet, SUFFIX[(s >> 24) & 7]);
    b->tip = (int)((s >> 27) % (sizeof(TIPS) / sizeof(TIPS[0])));
}

// --- Screens ----------------------------------------------------------------

void frontend_loading(float frac)
{
    ui_logo(160.0f, 84.0f, 1.0f);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                     FONT, 0, 150, "LOADING");
    ui_progress(80, 158, 160, frac);
}

void frontend_splash(uint32_t frames)
{
    ui_stars(frames);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                     FONT, 0, 96, "AN ORIGINAL HOMEBREW GAME");
    ui_heading(120, STYLE_NORMAL, "MADE WITH");
    ui_heading(142, STYLE_TITLE, "LIBDRAGON + TINY3D");
}

void frontend_title(uint32_t frames, const hiscore_table_t *t, bool saving)
{
    ui_shade(0, 0, 320, 78, 150);
    ui_shade(0, 196, 320, 240, 150);
    // The logo drops in over the first second, with a little overshoot.
    float k = frames < 30 ? (float)frames / 30.0f : 1.0f;
    float scale = 0.4f + 0.6f * k + 0.08f * k * (1.0f - k) * 4.0f;
    ui_logo(160.0f, 24.0f + 10.0f * (1.0f - k), scale);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                     FONT, 0, 70, "CO-OP SWARM DEFENSE FOR 1-4 TROOPERS");
    if ((frames / 20) % 3 != 2)
        ui_heading(172, STYLE_TITLE, "PRESS START");
    const hiscore_entry_t *top = &t->entries[0];
    rdpq_text_printf(&CENTRE, FONT, 0, 212, "HIGH SCORE %06lu  %.3s", (unsigned long)top->score, top->initials);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                     FONT, 0, 226, "%s", saving ? GAME_VERSION : "NO EEPROM: SCORES WON'T BE KEPT");
}

void frontend_menu(int cursor, uint32_t frames)
{
    (void)frames;
    ui_shade(0, 0, 320, 240, 110);
    ui_logo(160.0f, 24.0f, 0.75f);
    ui_box(90, 76, 230, 168);
    static const char *const ITEMS[MENU_COUNT] = { "DEPLOY", "HIGH SCORES", "OPTIONS", "CREDITS" };
    for (int i = 0; i < MENU_COUNT; i++) {
        char line[24];
        snprintf(line, sizeof(line), i == cursor ? "> %s <" : "%s", ITEMS[i]);
        ui_heading(100 + i * 20, i == cursor ? STYLE_TITLE : STYLE_NORMAL, line);
    }
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                     FONT, 0, 196, "A SELECT    B BACK");
}

static void volume_bar(int x, int y, int level)
{
    rdpq_set_mode_fill(RGBA32(40, 40, 48, 0xFF));
    for (int i = 0; i < 10; i++) {
        rdpq_set_fill_color(i < level ? RGBA32(0xE8, 0x96, 0x28, 0xFF) : RGBA32(40, 40, 48, 0xFF));
        rdpq_fill_rectangle(x + i * 8, y - 8, x + i * 8 + 6, y);
    }
}

void frontend_options(int cursor, const settings_t *s, int erase)
{
    ui_shade(0, 0, 320, 240, 110);
    ui_box(30, 40, 290, 196);
    ui_heading(64, STYLE_TITLE, "OPTIONS");
    const int x = 50, y0 = 92, lh = 18;
    static const char *const LABELS[OPT_COUNT] = { "MUSIC", "SOUND", "BOTS FILL EMPTY SLOTS", "ERASE HIGH SCORES", "BACK" };
    for (int i = 0; i < OPT_COUNT; i++) {
        int y = y0 + i * lh;
        int style = i == cursor ? STYLE_TITLE : STYLE_NORMAL;
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = (int16_t)style }, FONT, x, y, "%s %s",
                         i == cursor ? ">" : " ", LABELS[i]);
        if (i == OPT_MUSIC)
            volume_bar(180, y, s->music);
        else if (i == OPT_SFX)
            volume_bar(180, y, s->sfx);
        else if (i == OPT_BOTS)
            rdpq_text_printf(&(rdpq_textparms_t){ .style_id = (int16_t)style }, FONT, 220, y, "%s", s->bots ? "ON" : "OFF");
        else if (i == OPT_ERASE && i == cursor)
            ui_progress(180, y - 7, 80, (float)erase / ERASE_HOLD);
    }
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                     FONT, 0, 186, cursor == OPT_ERASE ? "HOLD A TO ERASE" : "LEFT/RIGHT CHANGE    B BACK");
}

void frontend_credits(void)
{
    ui_shade(0, 0, 320, 240, 150);
    ui_box(24, 30, 296, 210);
    ui_heading(54, STYLE_TITLE, "CREDITS");
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .line_spacing = 11 - FONT_LINE_H },
                     FONT, 0, 76,
                     "HIVEBREAKERS\n"
                     "^03AN ORIGINAL HOMEBREW GAME FOR THE N64^00\n"
                     "\n"
                     "^02BUILT WITH^00\n"
                     "LIBDRAGON (PREVIEW) AND TINY3D\n"
                     "\n"
                     "^02MUSIC AND SOUND^00\n"
                     "COMPOSED AND SYNTHESIZED FOR THIS GAME\n"
                     "\n"
                     "^02ART AND FONTS^00\n"
                     "MODELS, LOGO AND HEADING FONT: ORIGINAL\n"
                     "TEXT FONT: MONOGRAM BY DATAGOBLIN (CC0)\n"
                     "\n"
                     "^03B BACK^00");
}

void frontend_scores(const hiscore_table_t *t, int highlight, uint32_t frames, const char *footer)
{
    ui_shade(0, 0, 320, 240, 150);
    ui_box(24, 26, 296, 214);
    ui_heading(50, STYLE_TITLE, "HIGH SCORES");
    char text[640];
    static const char *const LEVEL[DIFFICULTY_COUNT] = { "RCN", "STD", "HAZ", "SEV", "HVB" };
    static const char *const OBJ[OBJ_COUNT] = { "ZONES", "NESTS", "UPLOAD" };
    int at = snprintf(text, sizeof(text), "^03  #  WHO   SCORE  LVL  OBJ     WAV  SQD^00");
    for (int i = 0; i < HISCORE_COUNT && at < (int)sizeof(text) - 64; i++) {
        const hiscore_entry_t *e = &t->entries[i];
        bool blink = i == highlight && (frames / 8) % 2;
        int style = i == highlight ? (blink ? STYLE_NORMAL : STYLE_GOOD) : i == 0 ? STYLE_TITLE : STYLE_NORMAL;
        at += snprintf(text + at, sizeof(text) - (size_t)at, "\n^%02X%2d.  %.3s  %6lu  %s  %-6s  %3u  %3u%s^00", style,
                       i + 1, e->initials, (unsigned long)e->score, LEVEL[e->difficulty % DIFFICULTY_COUNT],
                       OBJ[e->objective % OBJ_COUNT], e->waves, e->squad, (e->flags & HISCORE_EXTRACTED) ? "*" : "");
    }
    at += snprintf(text + at, sizeof(text) - (size_t)at, "\n^03%39s^00", "* EXTRACTED");
    rdpq_text_printf(&(rdpq_textparms_t){ .line_spacing = 11 - FONT_LINE_H }, FONT, 40, 68, "%s", text);
    if (footer)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                         FONT, 0, 206, "%s", footer);
}

void frontend_briefing(const briefing_t *b, const sim_t *sim, uint8_t connected, uint32_t frames)
{
    uint8_t humans = sim->humans;
    bool bots = sim->settings.bots;
    const mission_t *m = &sim->mission;
    ui_stars(frames);
    ui_planet(218, 12);
    rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STYLE_DIM }, FONT, 20, 36, "MISSION BRIEFING");
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 190, .style_id = STYLE_TITLE, .wrap = WRAP_WORD },
                     FONT_HEADING, 20, 58, "%s", b->operation);
    rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STYLE_DIM }, FONT, 20, 100, "PLANET %s", b->planet);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 300, .align = ALIGN_RIGHT, .style_id = STYLE_DIM }, FONT, 0, 100,
                     "REINF %u  SCORE X%d.%02d", sim->reinforcements, sim_difficulty(sim)->score / 100,
                     sim_difficulty(sim)->score % 100);
    // The first step depends on the objective.
    char first[64];
    int n = sim->arena.site_count;
    if (m->objective == OBJ_NESTS)
        snprintf(first, sizeof(first), "DESTROY THE %d ^04NESTS^00", n);
    else if (m->objective == OBJ_UPLOAD)
        snprintf(first, sizeof(first), "UPLOAD %d STAGES AT THE ^05TERMINAL^00", m->upload.stages);
    else if (n > 2)
        snprintf(first, sizeof(first), "HOLD ZONES ^02A^00, ^05B^00 AND ^06C^00");
    else
        snprintf(first, sizeof(first), "HOLD ZONES ^02A^00 AND ^05B^00");
    rdpq_text_printf(&(rdpq_textparms_t){ .line_spacing = 11 - FONT_LINE_H }, FONT, 20, 114,
                     "^021^00  %s\n"
                     "^022^00  CALL EXTRACTION AT THE LZ: ^02> > ^^ v^00\n"
                     "^023^00  HOLD OUT UNTIL THE DROPSHIP LANDS\n"
                     "^024^00  GET EVERYONE ABOARD",
                     first);
    rdpq_text_printf(NULL, FONT, 20, 162, "^03LEVEL^00 %s^02%s^00%s", m->difficulty > 0 ? "< " : "  ",
                     difficulty_defs[m->difficulty].name, m->difficulty + 1 < DIFFICULTY_COUNT ? " >" : "");
    // The squad: who's in, who could join.
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        int x = 20 + i * 72, y = 172;
        bool in = humans & (1u << i), plugged = connected & (1u << i);
        const uint8_t *c = PLAYER_RGB[i];
        rdpq_set_mode_fill(in ? RGBA32(c[0], c[1], c[2], 0xFF) : RGBA32(50, 50, 58, 0xFF));
        rdpq_fill_rectangle(x, y, x + 64, y + 2);
        const char *what = in ? "READY" : plugged ? ((frames / 15) % 2 ? "PUSH START" : "") : bots ? "AI" : "-";
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = (int16_t)(in || bots ? STYLE_P1 + i : STYLE_DIM) },
                         FONT, x, y + 14, "P%d", i + 1);
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = (int16_t)(in ? STYLE_NORMAL : STYLE_DIM) },
                         FONT, x, y + 26, "%s", what);
    }
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 280, .style_id = STYLE_DIM, .wrap = WRAP_WORD },
                     FONT, 20, 218, "TIP: %s", TIPS[b->tip]);
    if ((frames / 20) % 3 != 2)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 300, .align = ALIGN_RIGHT, .style_id = STYLE_TITLE },
                         FONT, 0, 162, "P1: START TO DROP");
}

void frontend_initials(const char initials[3], int pos, uint32_t score, int rank, uint32_t frames)
{
    ui_shade(0, 0, 320, 240, 160);
    ui_box(50, 56, 270, 184);
    ui_heading(84, (frames / 6) % 2 ? STYLE_GOOD : STYLE_TITLE, "NEW HIGH SCORE!");
    rdpq_text_printf(&CENTRE, FONT, 0, 104, "RANK %d     SCORE %lu", rank + 1, (unsigned long)score);
    char text[16];
    for (int k = 0; k < 3; k++) {
        int x = 160 - 30 + k * 22;
        snprintf(text, sizeof(text), "%c", initials[k]);
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = (int16_t)(k == pos ? STYLE_TITLE : STYLE_NORMAL) },
                         FONT_HEADING, (float)x, 140, "%s", text);
        if (k == pos && (frames / 10) % 2) {
            rdpq_set_mode_fill(RGBA32(0xE8, 0x96, 0x28, 0xFF));
            rdpq_fill_rectangle(x, 144, x + 10, 146);
        }
    }
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_DIM },
                     FONT, 0, 172, "UP/DOWN LETTER  A NEXT  B BACK  START DONE");
}
