#include "bench.h"
#include "calldown.h"
#include "difficulty.h"
#include "hud.h"
#include "mission.h"
#include "overlay.h"
#include "render.h"
#include "ui.h"
#include "weapons.h"

#include <libdragon.h>
#include <stdio.h>

#define FONT       FONT_BUILTIN_DEBUG_MONO
#define CW         6    // the debug font's advance, pixels
#define PANEL_COLS 12   // width of each player's panel, in characters

static const char ARROW[] = { [CODE_UP] = '^', [CODE_DOWN] = 'v', [CODE_LEFT] = '<', [CODE_RIGHT] = '>' };

// A bar on a dark backing, filled to `frac`. Expects fill mode: the caller
// sets it once for all its bars (each mode change costs the RSP).
static void bar(int x, int y, int w, int h, float frac, color_t fg)
{
    int fill = (int)((float)w * clampf(frac, 0.0f, 1.0f));
    rdpq_set_fill_color(RGBA32(20, 20, 20, 0xFF));
    rdpq_fill_rectangle(x - 1, y - 1, x + w + 1, y + h + 1);
    if (fill > 0) {
        rdpq_set_fill_color(fg);
        rdpq_fill_rectangle(x, y, x + fill, y + h);
    }
}

// Appends `text` (no style escapes) to `buf` at `*at` in `style`, padded with
// spaces to `width` columns. The style escape is only written when it
// changes (*cur tracks it): every style switch costs the RSP a mode change.
static void put_field(char *buf, int n, int *at, int *cur, int style, const char *text, int width)
{
    if (style != *cur && text[0]) {
        *at += snprintf(buf + *at, (size_t)(n - *at), "^%02X", style);
        *cur = style;
    }
    int len = snprintf(buf + *at, (size_t)(n - *at), "%-*s", width, text);
    *at += len < n - *at ? len : n - *at - 1;
}

// What a port's panel says under its label.
static const char *panel_status(const sim_t *sim, int idx, bool connected, char *buf, int n)
{
    static const char *const SHORT[WEAPON_COUNT] = { "RIF", "SHG", "MG", "GL" };
    const player_t *p = &sim->players[idx];
    bool human = sim->humans & (1u << idx);
    if (!human && !p->bot)
        return connected ? "PUSH START" : "";
    if (sim->mission.extracted & (1u << idx))
        return "ABOARD";
    if (!p->active)
        return "NEXT MISSION";
    if (p->state == PSTATE_DEAD)
        return "DOWN";
    if (p->state == PSTATE_DROPPING)
        return "DROPPING";
    const weapon_slot_t *w = &p->weapons[p->weapon];
    if (w->mag == 0 && w->reserve == 0 && !p->reload_timer)
        return "NO AMMO";
    snprintf(buf, (size_t)n, "%s %d/%d", SHORT[w->type], w->mag, w->reserve);
    return buf;
}

// One panel per port along the bottom: label in the player's colour (AI for
// a bot), health and reload bars, weapon and ammo or what they are waiting
// for. A port with a controller and nobody playing invites them to join.
// All four panels are one text call: each call is costly for the RSP.
static void player_panels(const sim_t *sim, uint8_t connected)
{
    const int x0 = 16, y = 208, w = PANEL_COLS * CW;
    char line1[96], line2[96];
    int at1 = 0, at2 = 0, style1 = STYLE_NORMAL, style2 = STYLE_NORMAL;
    rdpq_set_mode_fill(RGBA32(20, 20, 20, 0xFF));
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &sim->players[i];
        bool human = sim->humans & (1u << i);
        bool plugged = connected & (1u << i);
        char status[16];
        const char *st = panel_status(sim, i, plugged, status, sizeof(status));
        int cols = i + 1 < SIM_MAX_PLAYERS ? PANEL_COLS : 0;
        if (!human && !p->bot && !plugged) {
            put_field(line1, sizeof(line1), &at1, &style1, style1, "", cols);
            put_field(line2, sizeof(line2), &at2, &style2, style2, "", cols);
            continue;
        }
        bool playing = human || p->bot;
        char label[3] = { 'P', (char)('1' + i), 0 };
        put_field(line1, sizeof(line1), &at1, &style1, playing ? STYLE_P1 + i : STYLE_DIM, p->bot ? "AI" : label, cols);
        put_field(line2, sizeof(line2), &at2, &style2, playing ? STYLE_NORMAL : STYLE_DIM, st, cols);

        if (!sim_player_alive(p))
            continue;
        int x = x0 + i * w;
        float hp = p->hp / PLAYER_HP;
        color_t hp_col = hp > 0.5f ? RGBA32(80, 210, 90, 0xFF)
                       : hp > 0.25f ? RGBA32(230, 190, 50, 0xFF) : RGBA32(230, 60, 50, 0xFF);
        bar(x + 16, y - 7, w - 24, 5, hp, hp_col);
        if (p->reload_timer) {
            const weapon_def_t *def = &weapon_defs[p->weapons[p->weapon].type];
            bar(x + 16, y + 1, w - 24, 2, 1.0f - (float)p->reload_timer / (float)def->reload_ticks,
                RGBA32(220, 220, 220, 0xFF));
        }
    }
    rdpq_text_printf(NULL, FONT, x0, y, "%s\n%s", line1, line2);
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

// How many of `code`'s first inputs the entry so far matches, and whether
// the entry has gone past it (`out`).
static int code_match(const player_t *p, const uint8_t *code, int len, bool *out)
{
    int match = 0;
    while (match < p->code.len && match < len && code[match] == p->code.inputs[match])
        match++;
    *out = match < p->code.len;
    return match;
}

// While a player holds R: every calldown with its code (the part already
// entered in green; codes the entry no longer matches dimmed), or its
// cooldown. On an upload mission the last row is the terminal's code
// instead of Extraction (which can't be called before the objective is
// done anyway). Each player's panel has its own corner (P1 top left, P2
// top right, P3 and P4 below), so several can be open at once; one text
// call each.
static void code_panel(const sim_t *sim, int idx)
{
    const player_t *p = &sim->players[idx];
    if (!sim_player_alive(p) || !p->code.entering || !(sim->humans & (1u << idx))) // bots enter codes too
        return;
    // Rows: what everyone brings, then their loadout, then Extraction.
    enum { ROWS = 3 + LOADOUT_SLOTS };
    uint8_t rows[ROWS] = { CALLDOWN_REINFORCE, CALLDOWN_RESUPPLY };
    for (int k = 0; k < LOADOUT_SLOTS; k++)
        rows[2 + k] = sim->settings.loadout[idx][k];
    rows[ROWS - 1] = CALLDOWN_EXTRACTION;
    const int lh = 10, w = 24 * CW;
    const int x = idx % 2 ? 320 - 10 - w : 10, y0 = idx < 2 ? 43 : 43 + (ROWS + 1) * lh + 4;
    rdpq_set_mode_fill(RGBA32(10, 12, 18, 0xFF));
    rdpq_fill_rectangle(x - 4, y0 - 10, x + w + 2, y0 + ROWS * lh + 4);

    char text[512], code[64];
    int at = 0;
    code_text(code, sizeof(code), p->code.inputs, p->code.len, 0, STYLE_TITLE);
    at += snprintf(text + at, sizeof(text) - (size_t)at, "^%02XP%d CALLDOWN  %s", STYLE_P1 + idx, idx + 1, code);
    const mission_t *m = &sim->mission;
    bool terminal_row = m->objective == OBJ_UPLOAD && m->phase <= MISSION_OBJECTIVE;
    for (int r = 0; r < ROWS && at < (int)sizeof(text) - 64; r++) {
        int t = rows[r] < CALLDOWN_COUNT ? rows[r] : CALLDOWN_RESUPPLY;
        if (t == CALLDOWN_EXTRACTION && terminal_row) {
            const upload_t *u = &m->upload;
            bool open = mission_terminal_open(sim, idx), out;
            int match = code_match(p, u->code, u->code_len, &out);
            int style = open && !out ? STYLE_ZONE_B : STYLE_DIM;
            at += snprintf(text + at, sizeof(text) - (size_t)at, "\n^%02X%-12s", style, "UPLOAD");
            if (open) {
                code_text(code, sizeof(code), u->code, u->code_len, out ? 0 : match, style);
                at += snprintf(text + at, sizeof(text) - (size_t)at, "%s", code);
            } else {
                at += snprintf(text + at, sizeof(text) - (size_t)at, "%s", u->running ? "UPLOADING" : "AT TERMINAL");
            }
            continue;
        }
        const calldown_def_t *d = &calldown_defs[t];
        bool out;
        int match = code_match(p, d->code, d->code_len, &out);
        uint16_t cd = sim->cooldowns[idx][t];
        bool none = t == CALLDOWN_REINFORCE && sim->reinforcements == 0;
        bool ready = !cd && !none && calldown_available(sim, idx, (calldown_type_t)t);
        int style = out || !ready ? STYLE_DIM : STYLE_NORMAL;
        at += snprintf(text + at, sizeof(text) - (size_t)at, "\n^%02X%-12s", style, d->name);
        if (cd)
            at += snprintf(text + at, sizeof(text) - (size_t)at, "%ds", cd / SIM_HZ + 1);
        else if (none)
            at += snprintf(text + at, sizeof(text) - (size_t)at, "NONE");
        else {
            code_text(code, sizeof(code), d->code, d->code_len, out || !ready ? 0 : match, style);
            at += snprintf(text + at, sizeof(text) - (size_t)at, "%s", code);
        }
    }
    rdpq_text_printf(&(rdpq_textparms_t){ .line_spacing = lh - FONT_LINE_H }, FONT, x, y0, "%s", text);
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

// Over each live nest on screen, a health bar; over the terminal, the code
// to enter while it waits for one.
static void site_labels(const sim_t *sim)
{
    const mission_t *m = &sim->mission;
    if (m->phase != MISSION_OBJECTIVE)
        return;
    if (m->objective == OBJ_UPLOAD) {
        const upload_t *u = &m->upload;
        int sx, sy;
        if (u->running || !render_project(sim->arena.sites[0], 2.6f, &sx, &sy))
            return;
        char code[64];
        code_text(code, sizeof(code), u->code, u->code_len, 0, STYLE_ZONE_B);
        rdpq_text_printf(NULL, FONT, sx - (u->code_len * 2 - 1) * CW / 2, sy, "%s", code);
        return;
    }
    if (m->objective != OBJ_NESTS)
        return;
    bool mode = false;
    for (int n = 0; n < sim->arena.site_count; n++) {
        int sx, sy;
        if (m->nests[n].destroyed || !render_project(sim->arena.sites[n], 2.4f, &sx, &sy))
            continue;
        if (!mode)
            rdpq_set_mode_fill(RGBA32(20, 20, 20, 0xFF));
        mode = true;
        bar(sx - 14, sy - 2, 28, 3, m->nests[n].hp / mission_nest_hp(sim), RGBA32(150, 230, 60, 0xFF));
    }
}

// The objective's progress under the top line, with zone names in their
// marker colours (inline style escapes).
static void objective_progress(const sim_t *sim)
{
    const mission_t *m = &sim->mission;
    rdpq_textparms_t centre = { .width = 320, .align = ALIGN_CENTER };
    int total, done = mission_parts(sim, &total);
    if (m->objective == OBJ_NESTS) {
        rdpq_text_printf(&centre, FONT, 0, 30, "^%02XDESTROY THE NESTS^00   %d/%d", STYLE_TITLE, done, total);
    } else if (m->objective == OBJ_UPLOAD) {
        const upload_t *u = &m->upload;
        if (!u->running)
            rdpq_text_printf(&centre, FONT, 0, 30, "^%02XUPLOAD %d/%d^00   ENTER THE CODE AT THE TERMINAL", STYLE_TITLE,
                             done + 1, total);
        else
            rdpq_text_printf(&centre, FONT, 0, 30, "^%02XUPLOADING %d/%d^00  %d%%%s", STYLE_TITLE, done + 1, total,
                             u->progress * 100 / mission_stage_ticks(sim),
                             u->players_in ? "" : "  ^01STAY IN THE RING");
    } else {
        static const int ZONE_STYLE[3] = { STYLE_TITLE, STYLE_ZONE_B, STYLE_ZONE_C };
        char line[128];
        int at = snprintf(line, sizeof(line), "HOLD THE ZONES");
        for (int z = 0; z < total; z++) {
            int style = ZONE_STYLE[z % 3];
            if (m->zones[z].captured)
                at += snprintf(line + at, sizeof(line) - (size_t)at, "   ^%02X%c^%02X HELD^00", style, 'A' + z, STYLE_GOOD);
            else
                at += snprintf(line + at, sizeof(line) - (size_t)at, "   ^%02X%c^00 %d%%", style, 'A' + z,
                               m->zones[z].held * 100 / mission_capture_ticks(sim));
        }
        rdpq_text_printf(&centre, FONT, 0, 30, "%s", line);
    }
}

// What the squad should be doing, under the top line.
static void objective_line(const sim_t *sim)
{
    const mission_t *m = &sim->mission;
    rdpq_textparms_t centre = { .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_TITLE };
    switch (m->phase) {
    case MISSION_DROP:
        rdpq_text_printf(&centre, FONT, 0, 30, "DROPPING IN  -  %s", objective_name((objective_type_t)m->objective));
        break;
    case MISSION_OBJECTIVE:
        objective_progress(sim);
        break;
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
    if (sim->squad == SQUAD_LOST)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_WARN },
                         FONT, 0, 110, "SQUAD LOST  -  MISSION FAILED IN %d", sim->squad_timer / SIM_HZ + 1);
    else if (sim->squad == SQUAD_DOWN)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER }, FONT, 0, 110,
                         "SQUAD DOWN  -  EMERGENCY REINFORCE IN %d", sim->squad_timer / SIM_HZ + 1);
}

// Each person's control scheme (A on the pause screen switches it), the
// controls, how to join, and holding B to quit.
static void pause_screen(const sim_t *sim, int quit)
{
    char schemes[96];
    int at = snprintf(schemes, sizeof(schemes), "CONTROLS");
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (sim->humans & (1u << i))
            at += snprintf(schemes + at, sizeof(schemes) - (size_t)at, "   ^%02XP%d %c^00", STYLE_P1 + i, i + 1,
                           sim->settings.scheme[i] == SCHEME_B ? 'B' : 'A');
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = STYLE_TITLE },
                     FONT, 0, 50, "PAUSED");
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .line_spacing = 11 - FONT_LINE_H },
                     FONT, 0, 66,
                     "%s\n"
                     "^%02XA: SWITCH YOUR CONTROLS^00\n"
                     "\n"
                     "SCHEME A: STICK MOVE+AIM  Z FIRE+STRAFE\n"
                     "A DIVE\n"
                     "SCHEME B: STICK MOVE  C AIM+FIRE (8 DIR)\n"
                     "Z DIVE\n"
                     "BOTH: B TAP RELOAD  B HOLD SWITCH WEAPON\n"
                     "HOLD R + C-BUTTONS: CALLDOWN CODE\n"
                     "START ON ANOTHER CONTROLLER: JOIN\n"
                     "L+DDOWN DEBUG MENU   L+DUP STATS\n"
                     "^%02XHOLD B: QUIT TO TITLE^00",
                     schemes, STYLE_DIM, quit ? STYLE_WARN : STYLE_DIM);
    if (quit)
        ui_progress(110, 191, 100, (float)quit / 100.0f);
}

void hud_draw(const sim_t *sim, bool paused, uint8_t connected, int quit)
{
    if (!mission_over(sim)) // the results cover the bottom of the screen
        player_panels(sim, connected);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 296, .align = ALIGN_RIGHT }, FONT, 12, 16,
                     "^%02XREINF %u^00     WAVE %u  KILLS %lu  BUGS %u",
                     sim->reinforcements ? STYLE_NORMAL : STYLE_WARN, sim->reinforcements,
                     sim->waves.number, (unsigned long)sim->kills, sim->counts.enemies);
    const char *bench = bench_status();
    if (bench)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER }, FONT, 0, 42,
                         "%s", bench);

    objective_line(sim);
    site_labels(sim);
    beacon_labels(sim);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        code_panel(sim, i);
    squad_notice(sim);
    if (paused)
        pause_screen(sim, quit);
}
