// HIVEBREAKERS - N64 entry point.
//
// Frame loop: the sim ticks at a fixed rate locked to the video interrupt
// (one tick per 2 vblanks = ~29.91 Hz on NTSC). Each loop iteration waits
// until at least one tick is due, runs the due ticks (max 2), then renders
// the latest state once.
//
// Screens (frontend.h): splash -> title (logo over a demo the bots play) ->
// menu -> briefing (the loading screen; the mission is generated here and
// players join) -> mission (pause, results) -> initials for a high score ->
// high scores -> next mission's briefing, or back to the title.

#include <libdragon.h>
#include <t3d/t3d.h>

#include "autotest.h"
#include "bench.h"
#include "boot.h"
#include "campaign.h"
#include "difficulty.h"
#include "frontend.h"
#include "fx.h"
#include "gpu_timing.h"
#include "hud.h"
#include "input.h"
#include "menu.h"
#include "mission.h"
#include "overlay.h"
#include "render.h"
#include "results.h"
#include "save.h"
#include "sim.h"
#include "sound.h"
#include "tickclock.h"
#include "ui.h"
#include "version.h"

#define VBLANKS_PER_TICK    2
#define MAX_TICKS_PER_FRAME 2
#define HEAP_BASELINE_FRAME 30   // into a mission: let its first allocations settle
#define LEAVE_FRAMES        30   // a controller unplugged this long takes its player out
#define SPLASH_FRAMES       90
#define ATTRACT_IDLE_FRAMES 900  // title idle this long: show the high scores
#define ATTRACT_SCORES      300
#define DEMO_FRAMES         3600 // a new demo mission after this long
#define QUIT_HOLD_FRAMES    30   // hold B on the pause screen to quit
#define FADE_FRAMES         10
#define RESULTS_DELAY       60   // the mission is over: watch the dropship go before the results

static volatile uint32_t vblank_count;
static sim_t sim;
static tickclock_t tick_clock;

// Per-phase sim time for the current frame, summed over its ticks.
static uint32_t phase_ticks[TIMED_PHASES];
static uint32_t phase_start;
static sim_phase_t phase_cur;

// Screen state.
static screen_t screen;
static uint32_t screen_frames;
static int cursor;
static bool scores_attract;          // the table as part of the title's attract loop
static bool after_mission;           // the table after a mission: START goes on
static int highlight = -1;           // rank to blink on the table
static briefing_t brief;
static uint32_t mission_seed;
static uint32_t mission_frames;
static uint32_t over_frames;         // since the mission ended (the results wait for the dropship)
static bool paused, was_over;
static int quit_hold, erase_hold;
static mission_score_t score;
static int score_rank;               // where this mission's score lands, -1 if not
static bool score_eligible;
static char initials[3] = { 'A', 'A', 'A' };
static int initials_pos;
static int planet;                   // the mission's planet (campaign.h)
static uint8_t squad_next;           // who goes to the briefing from the sector map
static results_campaign_t liberation; // what the mission did for its planet
static uint32_t frames;              // since boot

static void on_vblank(void *arg)
{
    (void)arg;
    vblank_count++;
}

// Feed the audio, then spin until the VI interrupt has counted another
// vblank since this was called (one that happened while mixing counts: the
// frame must not wait for the next). Not vi_wait_vblank: that polls for the
// one scanline the VI interrupt fires on, so whenever the interrupt handlers
// outlast that line it misses every vblank and never returns (seen in ares;
// whether it happens depends on cache layout).
static void wait_vblank(void)
{
    uint32_t v = vblank_count;
    sound_update();
    while (vblank_count == v) {}
}

// A player whose controller has been unplugged for LEAVE_FRAMES leaves
// (player 1 never does): a bot takes over if bots are on.
static void update_leavers(uint8_t connected)
{
    static uint8_t gone[SIM_MAX_PLAYERS]; // frames without a controller
    for (int p = 1; p < SIM_MAX_PLAYERS; p++) {
        bool playing = sim.humans & (1u << p);
        if (!playing || (connected & (1u << p))) {
            gone[p] = 0;
        } else if (++gone[p] == LEAVE_FRAMES) {
            sim_player_leave(&sim, p);
            debugf("P%d left (controller unplugged)\n", p + 1);
        }
    }
}

// sim_step calls this at the start of each phase (and once at the end).
static void on_sim_mark(void *ctx, sim_phase_t phase)
{
    (void)ctx;
    uint32_t now = TICKS_READ();
    if (phase != SIM_PHASE_PLAYERS)
        phase_ticks[phase_cur] += now - phase_start;
    phase_cur = phase;
    phase_start = now;
}

// --- Boot loading screen ------------------------------------------------------

static int load_done, load_total;

static void load_step(void)
{
    load_done++;
    surface_t *fb = display_get();
    rdpq_attach_clear(fb, NULL);
    frontend_loading((float)load_done / (float)load_total);
    rdpq_detach_show();
}

static void load_step_n(int done, int total)
{
    (void)done;
    (void)total;
    load_step();
}

// --- Screen changes -------------------------------------------------------------

static void enter(screen_t s)
{
    screen = s;
    screen_frames = 0;
    cursor = 0;
    quit_hold = erase_hold = 0;
}

// After a new sim world: static matrices, effects, and no "dropped" ticks
// for the time building it took.
static void world_changed(void)
{
    render_bind_world(&sim);
    fx_init();
    tickclock_init(&tick_clock, VBLANKS_PER_TICK, MAX_TICKS_PER_FRAME, vblank_count);
}

static uint32_t new_seed(void)
{
    return (uint32_t)TICKS_READ() ^ (sim.rng.state * 2654435761u) ^ frames;
}

static void apply_settings(void)
{
    settings_t *st = save_settings();
    for (int p = 0; p < SIM_MAX_PLAYERS; p++)
        sim.settings.scheme[p] = st->scheme[p];
    sim.settings.bots = st->bots;
    sim.settings.difficulty = st->difficulty;
    sound_set_volumes(st->music, st->sfx);
}

// A new attract demo: a random objective and biome at the standard level.
static void new_demo(void)
{
    uint32_t seed = new_seed();
    sim.settings.objective = (uint8_t)((seed >> 5) % OBJ_COUNT);
    sim.settings.biome = (uint8_t)((seed >> 9) % BIOME_COUNT);
    sim.settings.difficulty = DIFFICULTY_STANDARD;
    sim_demo(&sim, seed);
    world_changed();
}

static void to_title(void)
{
    overlay_clear_heap_baseline();
    new_demo();
    paused = false;
    sound_duck(false);
    sound_music(MUSIC_TITLE);
    enter(SCREEN_TITLE);
}

// The briefing generates the mission (the loading screen) on the planet
// picked on the sector map: the squad is whoever is in `humans`; more can
// join until the drop.
static void to_briefing(uint8_t humans)
{
    overlay_clear_heap_baseline();
    mission_seed = new_seed();
    briefing_make(&brief, mission_seed);
    snprintf(brief.planet, sizeof(brief.planet), "%s", planet_defs[planet].name);
    apply_settings();
    sim.settings.biome = planet_defs[planet].biome;
    sim.settings.objective = (uint8_t)campaign_objective(planet, mission_seed >> 7);
#ifdef AUTOTEST
    sim.settings.objective = (uint8_t)autotest_objective();
#endif
    sim.humans = humans | 1u;
    sim_restart(&sim, mission_seed);
    world_changed();
    sound_music(MUSIC_TITLE);
    enter(SCREEN_BRIEFING);
}

// The sector map, on the last planet picked if it is still open.
static void to_sector(uint8_t humans)
{
    squad_next = humans;
    enter(SCREEN_SECTOR);
    cursor = save_settings()->planet;
    if (!campaign_open(save_campaign(), cursor))
        cursor = 0;
}

static void to_mission(void)
{
    paused = was_over = false;
    mission_frames = 0;
    world_changed();
    sound_duck(false);
    sound_music(MUSIC_MISSION);
    enter(SCREEN_MISSION);
}

static void to_scores(int hl, bool attract, bool after)
{
    highlight = hl;
    scores_attract = attract;
    after_mission = after;
    enter(SCREEN_SCORES);
}

// --- Per-screen input -----------------------------------------------------------

static void mission_input(const input_frame_t *in, uint8_t connected)
{
    update_leavers(connected);
    bool over = mission_over(&sim);
    if (in->toggle_menu) {
        menu_toggle();
        return;
    }
    if (menu_is_open()) {
        menu_update(in->pressed_any, &sim);
        return;
    }
    if (over) {
        // Results: START goes on to the initials (a new high score) or the table.
        nav_t n = frontend_nav(in, connected);
        if (n.start && over_frames > RESULTS_DELAY + 15) {
            sound_ui(UI_SELECT);
            if (score_eligible && score_rank >= 0) {
                initials_pos = 0;
                enter(SCREEN_INITIALS);
            } else {
                to_scores(-1, false, true);
            }
        }
        return;
    }
    // START on a controller nobody is playing joins in; from someone playing
    // it pauses.
    bool start = false;
    for (int p = 0; p < SIM_MAX_PLAYERS; p++) {
        if (!(in->ports[p].pressed & SIM_BTN_START))
            continue;
        if (!(sim.humans & (1u << p))) {
            bool now = sim_player_join(&sim, p);
            sound_ui(UI_JOIN);
            debugf("P%d joined%s\n", p + 1, now ? "" : " for the next mission");
            continue;
        }
        start = true;
    }
    if (start) {
        paused = !paused;
        sound_duck(paused);
        quit_hold = 0;
    }
    if (!paused)
        return;
    // On the pause screen, A switches that player's control scheme; player 1
    // holding B quits to the title.
    for (int p = 0; p < SIM_MAX_PLAYERS; p++) {
        if (!(sim.humans & (1u << p)) || !(in->ports[p].pressed & SIM_BTN_A))
            continue;
        uint8_t *scheme = &sim.settings.scheme[p];
        *scheme = (uint8_t)((*scheme + 1) % SCHEME_COUNT);
        save_settings()->scheme[p] = *scheme;
        sound_ui(UI_MOVE);
        debugf("P%d control scheme %c\n", p + 1, *scheme == SCHEME_A ? 'A' : 'B');
    }
    quit_hold = (in->ports[0].held & SIM_BTN_B) ? quit_hold + 1 : 0;
    if (quit_hold >= QUIT_HOLD_FRAMES) {
        debugf("quit to title\n");
        save_write_settings();
        to_title();
    }
}

static void frontend_input(const input_frame_t *in, uint8_t connected)
{
    nav_t n = frontend_nav(in, connected);
    settings_t *st = save_settings();
    switch (screen) {
    case SCREEN_SPLASH:
        if (screen_frames >= SPLASH_FRAMES || (screen_frames > 10 && (n.accept || n.back)))
            to_title();
        break;
    case SCREEN_TITLE:
        if (n.accept && screen_frames > 10) {
            sound_ui(UI_SELECT);
            enter(SCREEN_MENU);
        } else if (screen_frames >= ATTRACT_IDLE_FRAMES) {
            to_scores(-1, true, false);
        }
        break;
    case SCREEN_MENU:
        if (n.up || n.down) {
            cursor = (cursor + (n.up ? MENU_COUNT - 1 : 1)) % MENU_COUNT;
            sound_ui(UI_MOVE);
        } else if (n.back) {
            sound_ui(UI_BACK);
            enter(SCREEN_TITLE);
        } else if (n.accept) {
            sound_ui(UI_SELECT);
            switch (cursor) {
            case MENU_DEPLOY:  to_sector((uint8_t)(1u | (n.port > 0 ? 1u << n.port : 0))); break;
            case MENU_SCORES:  to_scores(-1, false, false); break;
            case MENU_OPTIONS: enter(SCREEN_OPTIONS); break;
            case MENU_CREDITS: enter(SCREEN_CREDITS); break;
            }
        }
        break;
    case SCREEN_OPTIONS: {
        if (n.up || n.down) {
            cursor = (cursor + (n.up ? OPT_COUNT - 1 : 1)) % OPT_COUNT;
            sound_ui(UI_MOVE);
        }
        int dir = n.left ? -1 : n.right ? 1 : 0;
        if (dir && cursor == OPT_MUSIC && st->music + dir >= 0 && st->music + dir <= 10) {
            st->music = (uint8_t)(st->music + dir);
            sound_set_volumes(st->music, st->sfx);
        } else if (dir && cursor == OPT_SFX && st->sfx + dir >= 0 && st->sfx + dir <= 10) {
            st->sfx = (uint8_t)(st->sfx + dir);
            sound_set_volumes(st->music, st->sfx);
            sound_ui(UI_MOVE); // hear the new level
        } else if ((dir || n.accept) && cursor == OPT_BOTS) {
            st->bots = !st->bots;
            sound_ui(UI_MOVE);
        }
        bool hold_a = false;
        for (int p = 0; p < SIM_MAX_PLAYERS; p++)
            hold_a |= (connected & (1u << p)) && (in->ports[p].held & SIM_BTN_A);
        erase_hold = (cursor == OPT_ERASE || cursor == OPT_RESET) && hold_a ? erase_hold + 1 : 0;
        if (erase_hold == ERASE_HOLD) {
            if (cursor == OPT_ERASE)
                save_erase_scores();
            else
                save_reset_campaign();
            sound_ui(UI_SELECT);
            debugf(cursor == OPT_ERASE ? "high scores erased\n" : "campaign reset\n");
        }
        if (n.back || (n.accept && cursor == OPT_BACK)) {
            save_write_settings();
            sound_ui(UI_BACK);
            enter(SCREEN_MENU);
            cursor = MENU_OPTIONS;
        }
        break;
    }
    case SCREEN_CREDITS:
        if (n.back || n.accept) {
            sound_ui(UI_BACK);
            enter(SCREEN_MENU);
            cursor = MENU_CREDITS;
        }
        break;
    case SCREEN_SCORES:
        if (scores_attract) {
            if (screen_frames >= ATTRACT_SCORES || n.accept || n.back)
                enter(SCREEN_TITLE);
        } else if (after_mission) {
            if (n.start) {
                sound_ui(UI_SELECT);
                to_sector(sim.humans);
            } else if (n.back) {
                sound_ui(UI_BACK);
                to_title();
            }
        } else if (n.back || n.accept) {
            sound_ui(UI_BACK);
            enter(SCREEN_MENU);
            cursor = MENU_SCORES;
        }
        break;
    case SCREEN_BRIEFING:
        // START on another controller joins (B leaves again); player 1's
        // START drops the squad.
        for (int p = 1; p < SIM_MAX_PLAYERS; p++) {
            const sim_input_t *i = &in->ports[p];
            uint8_t bit = (uint8_t)(1u << p);
            if ((i->pressed & SIM_BTN_START) && !(sim.humans & bit)) {
                sim_player_join(&sim, p);
                sound_ui(UI_JOIN);
            } else if (((i->pressed & SIM_BTN_B) || !i->connected) && (sim.humans & bit)) {
                sim_player_leave(&sim, p);
                sound_ui(UI_BACK);
            }
        }
        // Player 1 picks the difficulty (the mission is generated again: the
        // number of sites and the reinforcements depend on it). `n` repeats
        // held directions for every controller: take the ones P1 holds.
        const sim_input_t *i1 = &in->ports[0];
        bool p1_left = (i1->held & SIM_BTN_DLEFT) || i1->stick_x < -50;
        bool p1_right = (i1->held & SIM_BTN_DRIGHT) || i1->stick_x > 50;
        int diff = (int)st->difficulty + (n.right && p1_right ? 1 : n.left && p1_left ? -1 : 0);
        if (diff != st->difficulty && diff >= 0 && diff < DIFFICULTY_COUNT) {
            st->difficulty = (uint8_t)diff;
            sim.settings.difficulty = st->difficulty;
            sim_restart(&sim, mission_seed);
            world_changed();
            sound_ui(UI_MOVE);
        }
        if ((in->ports[0].pressed & (SIM_BTN_START | SIM_BTN_A)) && screen_frames > 15) {
            sound_ui(UI_SELECT);
            save_write_settings();
            to_mission();
        } else if (in->ports[0].pressed & SIM_BTN_B) {
            sound_ui(UI_BACK);
            to_title();
        }
        break;
    case SCREEN_SECTOR: {
        // Left/right (or up/down) step through the open planets.
        int step = n.right || n.down ? 1 : n.left || n.up ? -1 : 0;
        for (int k = 1; step && k < PLANET_COUNT; k++) {
            int next = (cursor + step * k + PLANET_COUNT) % PLANET_COUNT;
            if (campaign_open(save_campaign(), next)) {
                cursor = next;
                sound_ui(UI_MOVE);
                break;
            }
        }
        if (n.accept && screen_frames > 10) {
            sound_ui(UI_SELECT);
            planet = cursor;
            st->planet = (uint8_t)cursor;
            to_briefing(squad_next);
        } else if (n.back) {
            sound_ui(UI_BACK);
            enter(SCREEN_MENU);
        }
        break;
    }
    case SCREEN_INITIALS: {
        char *c = &initials[initials_pos];
        static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
        const int len = (int)sizeof(ALPHABET) - 1;
        int k = 0;
        while (k < len && ALPHABET[k] != *c)
            k++;
        if (n.up || n.down) {
            *c = ALPHABET[(k + (n.up ? 1 : len - 1)) % len];
            sound_ui(UI_MOVE);
        } else if ((n.right || (n.accept && !n.start)) && initials_pos < 2) {
            initials_pos++;
            sound_ui(UI_MOVE);
        } else if ((n.left || n.back) && initials_pos > 0) {
            initials_pos--;
            sound_ui(UI_BACK);
        } else if (n.start || (n.accept && initials_pos == 2)) {
            hiscore_entry_t e = { .score = (uint32_t)score.total, .waves = (uint8_t)sim.waves.number };
            for (int i = 0; i < 3; i++)
                e.initials[i] = initials[i];
            int squad = 0;
            for (int i = 0; i < SIM_MAX_PLAYERS; i++)
                squad += sim.players[i].active || (sim.mission.extracted & (1u << i));
            e.squad = (uint8_t)squad;
            e.flags = sim.mission.phase == MISSION_SUCCESS ? HISCORE_EXTRACTED : 0;
            e.difficulty = sim.mission.difficulty;
            e.objective = sim.mission.objective;
            int rank = hiscore_insert(save_scores(), &e);
            save_write_scores();
            save_write_settings();
            sound_ui(UI_SELECT);
            debugf("high score: rank %d %.3s %lu\n", rank + 1, e.initials, (unsigned long)e.score);
            to_scores(rank, false, true);
        }
        break;
    }
    default:
        break;
    }
}

// --- Drawing ---------------------------------------------------------------------

static bool world_visible(void)
{
    return screen != SCREEN_SPLASH && screen != SCREEN_BRIEFING && screen != SCREEN_SECTOR;
}

static void draw_2d(uint8_t connected)
{
    switch (screen) {
    case SCREEN_SPLASH:   frontend_splash(screen_frames); break;
    case SCREEN_TITLE:    frontend_title(screen_frames, save_scores(), save_available()); break;
    case SCREEN_MENU:     frontend_menu(cursor, screen_frames); break;
    case SCREEN_OPTIONS:  frontend_options(cursor, save_settings(), erase_hold); break;
    case SCREEN_CREDITS:  frontend_credits(); break;
    case SCREEN_SCORES:
        frontend_scores(save_scores(), highlight, screen_frames,
                        scores_attract ? NULL : after_mission ? "START NEXT MISSION    B TITLE" : "B BACK");
        break;
    case SCREEN_BRIEFING: frontend_briefing(&brief, &sim, connected, screen_frames); break;
    case SCREEN_MISSION:
        hud_draw(&sim, paused && !menu_is_open(), connected, paused ? quit_hold * 100 / QUIT_HOLD_FRAMES : 0);
        if (was_over && over_frames >= RESULTS_DELAY)
            results_draw(&sim, &score, score_eligible ? score_rank : -2, &liberation);
        break;
    case SCREEN_INITIALS:
        frontend_initials(initials, initials_pos, (uint32_t)score.total, score_rank, screen_frames);
        break;
    case SCREEN_SECTOR:
        frontend_sector(save_campaign(), cursor, screen_frames);
        break;
    }
    // Fade in on a new screen (not between the title and its menus).
    if (screen == SCREEN_SPLASH || screen == SCREEN_BRIEFING || screen == SCREEN_MISSION || screen == SCREEN_SECTOR)
        if (screen_frames < FADE_FRAMES)
            ui_fade(1.0f - (float)screen_frames / FADE_FRAMES);
}

int main(void)
{
    debug_init_usblog();
    debug_init_isviewer();
    debugf("HIVEBREAKERS %s (built %s) booting\n", GAME_VERSION, GAME_BUILD_DATE);

    boot_require_expansion_pak();
    debugf("RDRAM: %d KiB\n", get_memory_size() / 1024);

    dfs_init(DFS_DEFAULT_LOCATION);
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    display_set_fps_limit(display_get_refresh_rate() / VBLANKS_PER_TICK);
    rdpq_init();
    t3d_init((T3DInitParams){});

    input_init();
    overlay_init();
    ui_init();
    load_total = render_model_count() + sound_file_count();
    load_step(); // the loading screen's first frame
    load_done = 0;
    save_init();
    gpu_timing_init();
    render_init(load_step);
    sound_init(load_step_n);
    fx_init();
    sim_init(&sim, 0xC0FFEE);
    sim.mark = on_sim_mark;
    apply_settings();

    vi_install_vblank_handler(on_vblank, NULL);
    tickclock_init(&tick_clock, VBLANKS_PER_TICK, MAX_TICKS_PER_FRAME, vblank_count);
    debugf("init done, refresh %.3f Hz, save %s\n", (double)display_get_refresh_rate(),
           save_available() ? "EEPROM" : "none");

#ifdef BENCH_AT_BOOT
    to_briefing(1);
    // Always the same mission, whatever the save says, so benches compare.
    sim.settings.objective = OBJ_ZONES;
    sim.settings.difficulty = DIFFICULTY_STANDARD;
    sim_restart(&sim, mission_seed);
    to_mission();
#else
    enter(SCREEN_SPLASH);
#endif

    input_frame_t input;
    uint64_t last_frame = get_ticks();

    for (;;) {
        uint32_t ticks;
        while ((ticks = tickclock_advance(&tick_clock, vblank_count)) == 0)
            wait_vblank();

        uint64_t t_start = get_ticks();
        uint32_t frame_us = (uint32_t)TICKS_TO_US(t_start - last_frame);
        last_frame = t_start;

        input_poll(&input);
#ifdef AUTOTEST
        autotest_input(&input, &sim, screen, screen_frames, mission_frames);
#endif
        uint8_t connected = 0;
        for (int p = 0; p < SIM_MAX_PLAYERS; p++)
            connected |= (uint8_t)(input.ports[p].connected << p);
        if (input.toggle_overlay)
            overlay_toggle();

        screen_t before = screen;
        if (screen == SCREEN_MISSION)
            mission_input(&input, connected);
        else
            frontend_input(&input, connected);
        if (screen != before)
            ticks = 0; // a new screen starts on its next frame

        // Which world runs: the title's demo behind the front end, or the
        // mission while nothing covers it.
        bool demo = screen == SCREEN_TITLE || screen == SCREEN_MENU || screen == SCREEN_OPTIONS ||
                    screen == SCREEN_CREDITS || (screen == SCREEN_SCORES && !after_mission);
        bool playing = screen == SCREEN_MISSION && !paused && !menu_is_open() && !mission_over(&sim);
        uint32_t run = (demo || playing) ? ticks : 0;
        for (int i = 0; i < TIMED_PHASES; i++)
            phase_ticks[i] = 0;
        for (uint32_t i = 0; i < run; i++) {
            sim_step(&sim, input.ports);
            // Button edges belong to the first tick only.
            for (int p = 0; p < SIM_MAX_PLAYERS; p++)
                input.ports[p].pressed = 0;
        }
#ifdef AUTOTEST
        autotest_observe(&sim);
#endif
        if (screen == SCREEN_MISSION)
            sound_events(&sim);
        fx_spawn_from_events(&sim);
        sim_clear_events(&sim);
        fx_update(run);
        sim.counts.particles = (uint16_t)fx_count();
        uint64_t t_sim = get_ticks();
        sound_update();

        if (demo && (mission_over(&sim) || screen_frames % DEMO_FRAMES == DEMO_FRAMES - 1))
            new_demo();
        if (screen == SCREEN_MISSION) {
            if (++mission_frames == HEAP_BASELINE_FRAME)
                overlay_set_heap_baseline();
            if (mission_over(&sim) && !was_over) {
                was_over = true;
                overlay_clear_heap_baseline();
                score = mission_score(&sim);
                score_rank = hiscore_rank(save_scores(), (uint32_t)score.total);
#ifdef AUTOTEST
                score_eligible = true; // the script uses debug aids; let it reach the table anyway
#else
                score_eligible = !sim.mission.assisted;
#endif
                results_log(&sim, &score);
                // The planet's liberation, for missions that count.
                int parts, done = mission_parts(&sim, &parts);
                liberation = (results_campaign_t){ .planet = planet_defs[planet].name, .counted = score_eligible };
                if (score_eligible) {
                    liberation.gain = campaign_record(save_campaign(), planet, sim.mission.difficulty,
                                                      sim.mission.phase == MISSION_SUCCESS, done == parts,
                                                      &liberation.liberated);
                    liberation.complete = liberation.liberated && campaign_complete(save_campaign());
                    save_write_campaign();
                }
                liberation.now = save_campaign()->liberation[planet];
                debugf("CAMPAIGN planet=%d gain=%d now=%d%s%s\n", planet + 1, liberation.gain, liberation.now,
                       liberation.liberated ? " liberated" : "", liberation.complete ? " sector-liberated" : "");
                over_frames = 0;
            }
            over_frames++;
        }

        uint32_t wait_us = 0;
        if (world_visible()) {
            wait_us = render_scene(&sim, (float)run * SIM_DT);
        } else {
            uint64_t w0 = get_ticks();
            surface_t *fb = display_get();
            wait_us = (uint32_t)TICKS_TO_US(get_ticks() - w0);
            rdpq_attach_clear(fb, NULL);
        }
        uint64_t t_hud = get_ticks();
        draw_2d(connected);
        if (screen == SCREEN_MISSION)
            menu_draw(&sim);
        overlay_draw();
        uint32_t hud_us = (uint32_t)TICKS_TO_US(get_ticks() - t_hud);
        if (world_visible())
            render_end();
        else
            rdpq_detach_show();
        gpu_timing_frame();
        uint64_t t_end = get_ticks();
        sound_update();

        uint32_t sim_us = (uint32_t)TICKS_TO_US(t_sim - t_start);
        uint32_t total_us = (uint32_t)TICKS_TO_US(t_end - t_start);
        uint32_t render_us = (uint32_t)TICKS_TO_US(t_end - t_sim);
        const render_stats_t *rs = render_stats();
        // What is left of the render time once the framebuffer wait and the
        // CPU-side work are taken out is command issue, including any time
        // blocked on a full RSP queue.
        uint32_t cpu_side = wait_us + rs->anim_us + rs->build_us + hud_us;
        uint32_t submit_us = render_us > cpu_side ? render_us - cpu_side : 0;
        frame_timing_t timing = {
            .frame_us = frame_us,
            .cpu_us = total_us - wait_us,
            .sim_us = sim_us,
            .anim_us = rs->anim_us,
            .build_us = rs->build_us,
            .submit_us = submit_us,
            .hud_us = hud_us,
            .drawn_bugs = rs->drawn_bugs,
            .skeletons = rs->skeletons,
        };
        for (int i = 0; i < TIMED_PHASES; i++)
            timing.phase_us[i] = (uint32_t)TICKS_TO_US(phase_ticks[i]);
        overlay_record(&timing, &sim, &tick_clock);
        if (screen == SCREEN_MISSION)
            bench_frame(&sim, &timing, &tick_clock);
#ifdef BENCH_AT_BOOT
        if (screen == SCREEN_MISSION && mission_frames == 60)
            bench_start(&sim);
#endif
        frames++;
        screen_frames++;
    }
}
