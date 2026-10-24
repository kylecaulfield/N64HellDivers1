#include "results.h"
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

void results_draw(const sim_t *sim)
{
    const mission_stats_t *st = &sim->mission.stats;
    bool won = sim->mission.phase == MISSION_SUCCESS;
    const int x = 52, lh = 13;
    int y = 52;
    rdpq_set_mode_fill(RGBA32(10, 12, 18, 0xFF));
    rdpq_fill_rectangle(x - 16, y - 22, 320 - x + 16, y + 11 * lh + 4);

    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320 - 2 * x, .align = ALIGN_CENTER,
                                          .style_id = won ? STYLE_GOOD : STYLE_WARN },
                     FONT, x, y, won ? "MISSION COMPLETE" : "MISSION FAILED");
    y += 2 * lh;
    int sec = (int)(st->ticks / SIM_HZ);
    unsigned hit = st->shots ? (unsigned)(st->hits * 100 / st->shots) : 0;
    rdpq_text_printf(NULL, FONT, x, y, "TIME            %d:%02d", sec / 60, sec % 60);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "WAVES           %u", sim->waves.number);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "KILLS           %d", total_kills(st));
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "  skitters %u  gnashers %u  bulwarks %u",
                     st->kills[ENEMY_SKITTER], st->kills[ENEMY_GNASHER], st->kills[ENEMY_BULWARK]);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "SHOTS           %lu   HITS %u%%", (unsigned long)st->shots, hit);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "CALLDOWNS       %u", st->calldowns);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "DEATHS          %u   REINFORCED %u", st->deaths, st->reinforcements);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "FRIENDLY HITS   %u", st->friendly_hits);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "EXTRACTED       %d / %d", count_bits(sim->mission.extracted), squad_size(sim));
    y += lh + 6;
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320 - 2 * x, .align = ALIGN_CENTER, .style_id = STYLE_TITLE },
                     FONT, x, y, "PRESS START FOR A NEW MISSION");
}

void results_log(const sim_t *sim)
{
    const mission_stats_t *st = &sim->mission.stats;
    debugf("RESULT build=%s outcome=%s time=%lus waves=%u kills=%u/%u/%u shots=%lu hits=%lu calldowns=%u "
           "deaths=%u reinforced=%u friendly_hits=%u extracted=%d/%d\n",
           GAME_VERSION, sim->mission.phase == MISSION_SUCCESS ? "success" : "failed",
           (unsigned long)(st->ticks / SIM_HZ), sim->waves.number,
           st->kills[ENEMY_SKITTER], st->kills[ENEMY_GNASHER], st->kills[ENEMY_BULWARK],
           (unsigned long)st->shots, (unsigned long)st->hits, st->calldowns, st->deaths, st->reinforcements,
           st->friendly_hits, count_bits(sim->mission.extracted), squad_size(sim));
}
