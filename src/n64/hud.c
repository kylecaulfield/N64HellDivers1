#include "bench.h"
#include "calldown.h"
#include "hud.h"
#include "mission.h"
#include "overlay.h"
#include "render.h"
#include "weapons.h"

#include <libdragon.h>
#include <stdio.h>

#define FONT FONT_BUILTIN_DEBUG_MONO
#define CW   6   // the debug font's advance, pixels

static const char ARROW[] = { [CODE_UP] = '^', [CODE_DOWN] = 'v', [CODE_LEFT] = '<', [CODE_RIGHT] = '>' };

static void bar(int x, int y, int w, int h, float frac, color_t fg)
{
    int fill = (int)((float)w * clampf(frac, 0.0f, 1.0f));
    rdpq_set_mode_fill(RGBA32(20, 20, 20, 0xFF));
    rdpq_fill_rectangle(x - 1, y - 1, x + w + 1, y + h + 1);
    if (fill > 0) {
        rdpq_set_mode_fill(fg);
        rdpq_fill_rectangle(x, y, x + fill, y + h);
    }
}

static void player_panel(const sim_t *sim, int idx, int x, int y)
{
    const player_t *p = &sim->players[idx];
    const weapon_slot_t *w = &p->weapons[p->weapon];
    const weapon_def_t *def = &weapon_defs[w->type];

    float hp = p->hp / PLAYER_HP;
    color_t hp_col = hp > 0.5f ? RGBA32(80, 210, 90, 0xFF)
                   : hp > 0.25f ? RGBA32(230, 190, 50, 0xFF) : RGBA32(230, 60, 50, 0xFF);
    bar(x + 26, y - 7, 70, 5, hp, hp_col);
    if (p->reload_timer)
        bar(x + 26, y + 5, 70, 2, 1.0f - (float)p->reload_timer / (float)def->reload_ticks,
            RGBA32(220, 220, 220, 0xFF));

    // Few text calls: each one is costly for the RSP (see render.c).
    rdpq_text_printf(NULL, FONT, x, y, "P%d", idx + 1);
    const char *status = sim->settings.scheme[idx] == SCHEME_B ? "CONTROLS B" : "CONTROLS A";
    if (p->reload_timer)
        status = "RELOADING";
    else if (w->mag == 0)
        status = w->reserve ? "RELOAD (B)" : "NO AMMO";
    rdpq_text_printf(&(rdpq_textparms_t){ .line_spacing = 3 }, FONT, x + 102, y, "%s %d/%d\n%s",
                     def->name, w->mag, w->reserve, status);
}

// A code as arrows for rdpq_text ("^" must be written "^^"), the first
// `matched` inputs in STYLE_GOOD and the rest in `style`, using inline style
// escapes ("^XX"), which take no width.
static void code_text(char *buf, int n, const uint8_t *code, int len, int matched, int style)
{
    int at = snprintf(buf, n, "^%02X", matched ? STYLE_GOOD : style);
    for (int k = 0; k < len && at < n - 8; k++) {
        if (k == matched && matched)
            at += snprintf(buf + at, n - at, "^%02X", style);
        const char *sep = k + 1 < len ? " " : "";
        if (code[k] == CODE_UP)
            at += snprintf(buf + at, n - at, "^^%s", sep);
        else
            at += snprintf(buf + at, n - at, "%c%s", ARROW[code[k]], sep);
    }
}

static void calldown_status(const sim_t *sim, int idx, calldown_type_t type, char *buf, int n)
{
    uint16_t cd = sim->cooldowns[idx][type];
    if (cd)
        snprintf(buf, n, "%ds", cd / SIM_HZ + 1);
    else if (type == CALLDOWN_REINFORCE && sim->reinforcements == 0)
        snprintf(buf, n, "NONE");
    else if (!calldown_available(sim, idx, type))
        snprintf(buf, n, "--");
    else
        snprintf(buf, n, "READY");
}

// While a player holds R: every calldown with its code (the part already
// entered in green; codes the entry no longer matches dimmed) and status.
static void code_panel(const sim_t *sim, int idx)
{
    const player_t *p = &sim->players[idx];
    if (!sim_player_alive(p) || !p->code.entering)
        return;
    const int x = 10, y0 = 46, lh = 11;
    rdpq_set_mode_fill(RGBA32(10, 12, 18, 0xFF));
    rdpq_fill_rectangle(x - 6, y0 - 10, x + 31 * CW, y0 + (CALLDOWN_COUNT + 1) * lh - 4);

    char buf[64];
    code_text(buf, sizeof(buf), p->code.inputs, p->code.len, 0, STYLE_TITLE);
    rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STYLE_TITLE }, FONT, x, y0, "CALLDOWN  %s", buf);
    for (int t = 0; t < CALLDOWN_COUNT; t++) {
        const calldown_def_t *d = &calldown_defs[t];
        int y = y0 + (t + 1) * lh;
        int match = 0;
        while (match < p->code.len && match < d->code_len && d->code[match] == p->code.inputs[match])
            match++;
        bool out = match < p->code.len;
        char status[8];
        calldown_status(sim, idx, (calldown_type_t)t, status, sizeof(status));
        bool ready = status[0] == 'R';
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = out || !ready ? STYLE_DIM : STYLE_NORMAL },
                         FONT, x, y, "%s", d->name);
        code_text(buf, sizeof(buf), d->code, d->code_len, out ? 0 : match, out ? STYLE_DIM : STYLE_NORMAL);
        rdpq_text_printf(NULL, FONT, x + 12 * CW, y, "%s", buf);
        rdpq_text_printf(&(rdpq_textparms_t){ .style_id = ready ? STYLE_GOOD : STYLE_DIM },
                         FONT, x + 25 * CW, y, "%s", status);
    }
}

// Seconds to arrival over each armed beacon.
static void beacon_labels(const sim_t *sim)
{
    for (int i = 0; i < SIM_MAX_BEACONS; i++) {
        const beacon_t *b = &sim->beacons[i];
        int sx, sy;
        if (!b->active || b->state != BEACON_ARMED || !render_project(b->pos, 3.8f, &sx, &sy))
            continue;
        rdpq_text_printf(NULL, FONT, sx - CW / 2, sy, "%d", b->timer / SIM_HZ + 1);
    }
}

// What the squad should be doing, under the top line.
static void objective_line(const sim_t *sim)
{
    const mission_t *m = &sim->mission;
    rdpq_textparms_t centre = { .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_TITLE };
    switch (m->phase) {
    case MISSION_DROP:
        rdpq_text_printf(&centre, FONT, 0, 30, "DROPPING IN");
        break;
    case MISSION_OBJECTIVE: {
        // Zone names in their marker colours, via inline style escapes.
        char zone[MISSION_ZONES][20];
        for (int z = 0; z < MISSION_ZONES; z++) {
            int style = z ? STYLE_ZONE_B : STYLE_TITLE;
            if (m->zones[z].captured)
                snprintf(zone[z], sizeof(zone[z]), "^%02X%c^%02X HELD", style, 'A' + z, STYLE_GOOD);
            else
                snprintf(zone[z], sizeof(zone[z]), "^%02X%c^00 %d%%", style, 'A' + z,
                         m->zones[z].held * 100 / mission_capture_ticks(sim));
        }
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER }, FONT, 0, 30,
                         "HOLD THE ZONES   %s^00   %s", zone[0], zone[1]);
        break;
    }
    case MISSION_EXTRACT:
        rdpq_text_printf(&centre, FONT, 0, 30, "GO TO THE LZ AND CALL EXTRACTION");
        break;
    case MISSION_HOLD: {
        int sec = m->timer / SIM_HZ + 1;
        rdpq_text_printf(&centre, FONT, 0, 30, "HOLD OUT  -  DROPSHIP IN %d:%02d", sec / 60, sec % 60);
        break;
    }
    case MISSION_BOARDING:
        rdpq_text_printf(&centre, FONT, 0, 30, "DROPSHIP DOWN  -  GET TO THE LZ");
        break;
    }
}

static void squad_notice(const sim_t *sim)
{
    rdpq_textparms_t centre = { .width = 320, .align = ALIGN_CENTER };
    const player_t *p1 = &sim->players[0];
    if (sim->squad == SQUAD_LOST)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_WARN },
                         FONT, 0, 110, "SQUAD LOST  -  MISSION FAILED IN %d", sim->squad_timer / SIM_HZ + 1);
    else if (sim->squad == SQUAD_DOWN)
        rdpq_text_printf(&centre, FONT, 0, 110, "SQUAD DOWN  -  EMERGENCY REINFORCE IN %d",
                         sim->squad_timer / SIM_HZ + 1);
    else if (p1->active && p1->state == PSTATE_DEAD)
        rdpq_text_printf(&centre, FONT, 0, 110, "DOWN  -  WAITING FOR A REINFORCE");
}

void hud_draw(const sim_t *sim, bool paused)
{
    player_panel(sim, 0, 12, 214);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 296, .align = ALIGN_RIGHT }, FONT, 12, 16,
                     "^%02XREINF %u^00     WAVE %u  KILLS %lu  BUGS %u",
                     sim->reinforcements ? STYLE_NORMAL : STYLE_WARN, sim->reinforcements,
                     sim->waves.number, (unsigned long)sim->kills, sim->counts.enemies);
    const char *bench = bench_status();
    if (bench)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER }, FONT, 0, 42,
                         "%s", bench);

    objective_line(sim);
    beacon_labels(sim);
    code_panel(sim, 0);
    squad_notice(sim);

    if (paused) {
        rdpq_textparms_t centre = { .width = 320, .align = ALIGN_CENTER };
        rdpq_text_printf(&centre, FONT, 0, 70, "PAUSED");
        rdpq_text_printf(&centre, FONT, 0, 96, "SCHEME A: STICK MOVE+AIM  Z FIRE+STRAFE");
        rdpq_text_printf(&centre, FONT, 0, 108, "A DIVE");
        rdpq_text_printf(&centre, FONT, 0, 126, "SCHEME B: STICK MOVE  C AIM+FIRE (8 DIR)");
        rdpq_text_printf(&centre, FONT, 0, 138, "Z DIVE");
        rdpq_text_printf(&centre, FONT, 0, 156, "BOTH: B TAP RELOAD  B HOLD SWITCH WEAPON");
        rdpq_text_printf(&centre, FONT, 0, 168, "HOLD R + C-BUTTONS: CALLDOWN CODE");
        rdpq_text_printf(&centre, FONT, 0, 186, "L+DDOWN DEBUG MENU   L+DUP STATS");
    }
}
