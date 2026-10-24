#include "results.h"
#include "mission.h"
#include "overlay.h"
#include "version.h"

#include <libdragon.h>

#define FONT FONT_BUILTIN_DEBUG_MONO

static int count_bits(uint8_t v)
{
    int n = 0;
    for (; v; v &= (uint8_t)(v - 1))
        n++;
    return n;
}

// Players who were in the mission: those extracted plus those still on the
// ground (alive or dead).
static int squad_size(const sim_t *sim)
{
    int n = count_bits(sim->mission.extracted);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        n += sim->players[i].active;
    return n;
}

static int total_kills(const mission_stats_t *st)
{
    int n = 0;
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
        n += st->kills[t];
    return n;
}

// Ports that took part: playing at the end (as a person or a bot), aboard,
// or with anything to show for the mission.
static bool took_part(const sim_t *sim, int idx)
{
    const player_stats_t *p = &sim->mission.stats.players[idx];
    return sim->players[idx].active || (sim->mission.extracted & (1u << idx)) ||
           p->shots || p->kills || p->deaths || p->calldowns;
}

// How a port's mission ended.
static const char *outcome(const sim_t *sim, int idx)
{
    const player_t *p = &sim->players[idx];
    if (sim->mission.extracted & (1u << idx))
        return "aboard";
    if (!p->active)
        return "gone"; // left the game
    return p->state == PSTATE_DEAD ? "down" : "left";
}

static unsigned accuracy(const player_stats_t *p)
{
    return p->shots ? (unsigned)(p->hits * 100 / p->shots) : 0;
}

void results_draw(const sim_t *sim)
{
    const mission_stats_t *st = &sim->mission.stats;
    bool won = sim->mission.phase == MISSION_SUCCESS;
    const int x = 22, y0 = 48;
    rdpq_set_mode_fill(RGBA32(10, 12, 18, 0xFF));
    rdpq_fill_rectangle(x - 8, y0 - 18, 320 - x + 8, y0 + 150);

    rdpq_textparms_t centre = { .width = 320 - 2 * x, .align = ALIGN_CENTER };
    centre.style_id = won ? STYLE_GOOD : STYLE_WARN;
    rdpq_text_printf(&centre, FONT, x, y0, won ? "MISSION COMPLETE" : "MISSION FAILED");

    // The squad, then a row per player in their colour, as one text call.
    char text[640];
    int sec = (int)(st->ticks / SIM_HZ);
    int at = snprintf(text, sizeof(text),
                      "TIME %d:%02d    WAVES %u    REINFORCEMENTS %u\n"
                      "KILLS %d: %u skitters %u gnashers %u bulwarks\n"
                      "EXTRACTED %d OF %d\n"
                      "\n"
                      "^%02X      KILLS  DEATHS   ACC  FF  CALLS^00",
                      sec / 60, sec % 60, sim->waves.number, st->reinforcements,
                      total_kills(st), st->kills[ENEMY_SKITTER], st->kills[ENEMY_GNASHER],
                      st->kills[ENEMY_BULWARK], count_bits(sim->mission.extracted), squad_size(sim), STYLE_DIM);
    for (int i = 0; i < SIM_MAX_PLAYERS && at < (int)sizeof(text) - 80; i++) {
        if (!took_part(sim, i))
            continue;
        const player_stats_t *p = &st->players[i];
        char label[3] = { 'P', (char)('1' + i), 0 };
        at += snprintf(text + at, sizeof(text) - (size_t)at, "\n^%02X%-4s^00 %6u  %6u  %3u%%  %2u  %5u  %s",
                       STYLE_P1 + i, sim->players[i].bot ? "AI" : label, p->kills, p->deaths, accuracy(p),
                       p->friendly_hits, p->calldowns, outcome(sim, i));
    }
    rdpq_text_printf(&(rdpq_textparms_t){ .line_spacing = 12 - FONT_LINE_H }, FONT, x, y0 + 22, "%s", text);

    centre.style_id = STYLE_TITLE;
    rdpq_text_printf(&centre, FONT, x, y0 + 144, "PRESS START FOR A NEW MISSION");
}

void results_log(const sim_t *sim)
{
    const mission_stats_t *st = &sim->mission.stats;
    player_stats_t sq = mission_squad_stats(st);
    // Per port: kills/deaths/shots/hits/friendly_hits/calldowns/outcome,
    // as pN= for a person and aiN= for a bot.
    char per[200];
    int at = 0;
    per[0] = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS && at < (int)sizeof(per) - 48; i++) {
        if (!took_part(sim, i))
            continue;
        const player_stats_t *p = &st->players[i];
        at += snprintf(per + at, sizeof(per) - (size_t)at, " %s%d=%u/%u/%lu/%lu/%u/%u/%s",
                       sim->players[i].bot ? "ai" : "p", i + 1, p->kills, p->deaths, (unsigned long)p->shots,
                       (unsigned long)p->hits, p->friendly_hits, p->calldowns, outcome(sim, i));
    }
    debugf("RESULT build=%s outcome=%s time=%lus waves=%u kills=%u/%u/%u shots=%lu hits=%lu calldowns=%u "
           "deaths=%u reinforced=%u friendly_hits=%u extracted=%d/%d%s\n",
           GAME_VERSION, sim->mission.phase == MISSION_SUCCESS ? "success" : "failed",
           (unsigned long)(st->ticks / SIM_HZ), sim->waves.number,
           st->kills[ENEMY_SKITTER], st->kills[ENEMY_GNASHER], st->kills[ENEMY_BULWARK],
           (unsigned long)sq.shots, (unsigned long)sq.hits, sq.calldowns, sq.deaths, st->reinforcements,
           sq.friendly_hits, count_bits(sim->mission.extracted), squad_size(sim), per);
}
