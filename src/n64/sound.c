#include "sound.h"

#include <libdragon.h>
#include <math.h>

#include "camera.h"

#define OUTPUT_HZ     22050
// Mixer channels: three banks of 8 for the music, then the effects. The
// mission and hold tracks each own a bank: the mixer sizes a channel's
// buffers for the track that plays on it, and reallocates them when another
// track's limits differ, so tracks that change mid-mission must not share.
// The front end's tracks share the third. (Separate banks also mean a track
// that is stopping, which xm64player_stop does on the next mixer poll,
// never cuts the incoming one.)
#define MUSIC_CH      8
#define SFX_CH0       (3 * MUSIC_CH)
#define SFX_CHANNELS  8
#define MUSIC_GAIN    0.55f   // music sits under the effects

typedef enum {
    SFX_RIFLE, SFX_SHOTGUN, SFX_MG, SFX_EXPLOSION, SFX_IMPACT, SFX_BUG_HIT, SFX_ARMOR_HIT,
    SFX_BUG_DIE, SFX_PLAYER_HURT, SFX_PLAYER_DIE, SFX_DRY_FIRE, SFX_RELOAD, SFX_CODE_BEEP,
    SFX_CODE_REJECT, SFX_BEACON, SFX_INCOMING, SFX_PICKUP, SFX_ZONE, SFX_MENU_MOVE,
    SFX_MENU_SELECT, SFX_RADIO, SFX_ALARM, SFX_COUNT
} sfx_t;

static const char *const SFX_FILES[SFX_COUNT] = {
    "rifle", "shotgun", "mg", "explosion", "impact", "bug_hit", "armor_hit",
    "bug_die", "player_hurt", "player_die", "dry_fire", "reload", "code_beep",
    "code_reject", "beacon", "incoming", "pickup", "zone", "menu_move",
    "menu_select", "radio", "alarm",
};
static const char *const MUSIC_FILES[MUSIC_COUNT] = { "title", "mission", "hold", "victory", "defeat" };

static wav64_t *sfx[SFX_COUNT];
static xm64player_t music[MUSIC_COUNT];
static music_t playing = MUSIC_NONE;
static const int BANK[MUSIC_COUNT] = {
    [MUSIC_TITLE] = 2, [MUSIC_MISSION] = 0, [MUSIC_HOLD] = 1, [MUSIC_VICTORY] = 2, [MUSIC_DEFEAT] = 2,
};
static float music_vol = 0.7f, sfx_vol = 0.8f;
static bool ducked;
static uint16_t last_wave;        // to sound the alarm when a wave starts

int sound_file_count(void)
{
    return SFX_COUNT + MUSIC_COUNT;
}

void sound_init(void (*progress)(int done, int total))
{
    audio_init(OUTPUT_HZ, AUDIO_DEFAULT_LATENCY);
    mixer_init(SFX_CH0 + SFX_CHANNELS);
    int done = 0, total = sound_file_count();
    for (int i = 0; i < SFX_COUNT; i++) {
        char path[48];
        snprintf(path, sizeof(path), "rom:/sfx/%s.wav64", SFX_FILES[i]);
        // Preloaded: no ROM streaming while the game runs.
        sfx[i] = wav64_load(path, &(wav64_loadparms_t){ .streaming_mode = WAV64_STREAMING_NONE });
        if (progress)
            progress(++done, total);
    }
    // Each effects channel allocates a small codec state the first time it
    // plays a preloaded wave (and swaps it for an equal one after): do that
    // now, silently, so the heap doesn't grow mid-mission.
    for (int ch = SFX_CH0; ch < SFX_CH0 + SFX_CHANNELS; ch++) {
        wav64_play(sfx[SFX_DRY_FIRE], ch);
        mixer_ch_set_vol(ch, 0.0f, 0.0f);
        mixer_ch_stop(ch);
    }
    for (int i = 0; i < MUSIC_COUNT; i++) {
        char path[48];
        snprintf(path, sizeof(path), "rom:/music/%s.xm64", MUSIC_FILES[i]);
        xm64player_open(&music[i], path);
        xm64player_set_loop(&music[i], i != MUSIC_VICTORY && i != MUSIC_DEFEAT);
        if (progress)
            progress(++done, total);
    }
    // Let the in-game tracks allocate their channel buffers now, silently,
    // from a row where every channel plays, so nothing allocates mid-mission.
    static const int FULL_ORDER[MUSIC_COUNT] = { [MUSIC_MISSION] = 2, [MUSIC_HOLD] = 1 };
    for (int m = MUSIC_MISSION; m <= MUSIC_HOLD; m++) {
        xm64player_set_vol(&music[m], 0.0f);
        xm64player_seek(&music[m], FULL_ORDER[m], 0, 0);
        xm64player_play(&music[m], BANK[m] * MUSIC_CH);
        for (int k = 0; k < 20; k++) {
            mixer_try_play();
            wait_ms(10);
        }
        xm64player_stop(&music[m]);
        for (int k = 0; k < 5; k++) {
            mixer_try_play();
            wait_ms(10);
        }
    }
}

void sound_update(void)
{
    mixer_try_play();
}

static float music_level(void)
{
    return music_vol * MUSIC_GAIN * (ducked ? 0.35f : 1.0f);
}

void sound_music(music_t m)
{
    if (m == playing)
        return;
    if (playing != MUSIC_NONE)
        xm64player_stop(&music[playing]);
    playing = m;
    if (m == MUSIC_NONE)
        return;
    xm64player_seek(&music[m], 0, 0, 0);
    xm64player_set_vol(&music[m], music_level());
    xm64player_play(&music[m], BANK[m] * MUSIC_CH);
}

void sound_set_volumes(int m, int s)
{
    music_vol = (float)m / 10.0f;
    sfx_vol = (float)s / 10.0f;
    if (playing != MUSIC_NONE)
        xm64player_set_vol(&music[playing], music_level());
}

void sound_duck(bool on)
{
    if (on == ducked)
        return;
    ducked = on;
    if (playing != MUSIC_NONE)
        xm64player_set_vol(&music[playing], music_level());
}

// Play on a free effects channel, or steal one of no higher priority.
// pan: 0 left .. 1 right.
static void play(sfx_t id, int priority, float vol, float pan)
{
    int ch;
    if (vol * sfx_vol < 0.01f ||
        mixer_ch_alloc(SFX_CH0, SFX_CHANNELS, 1, false, priority, &sfx[id]->wave, &ch) < 1)
        return;
    mixer_ch_stop(ch);
    wav64_play(sfx[id], ch);
    // vol_pan halves a centred sound; double it back (the mixer clamps).
    mixer_ch_set_vol_pan(ch, fminf(1.0f, vol * sfx_vol * 2.0f), pan);
    mixer_ch_set_priority(ch, priority);
}

void sound_ui(ui_sound_t u)
{
    static const sfx_t MAP[] = {
        [UI_MOVE] = SFX_MENU_MOVE, [UI_SELECT] = SFX_MENU_SELECT, [UI_BACK] = SFX_MENU_MOVE,
        [UI_JOIN] = SFX_RADIO, [UI_ERROR] = SFX_CODE_REJECT,
    };
    play(MAP[u], 5, u == UI_BACK ? 0.5f : 0.9f, 0.5f);
}

// Where a sound at `pos` sits: pan across the screen, quieter far from the
// camera's target.
static void place(const sim_t *s, vec2 pos, float *vol, float *pan)
{
    const camera_t *c = &s->camera;
    cam_extent_t e = camera_extent();
    float half = c->dist * e.half_w_top;
    float dx = (pos.x - c->target.x) / (half > 1.0f ? half : 1.0f);
    *pan = 0.5f + 0.4f * fmaxf(-1.0f, fminf(1.0f, dx));
    float d = v2_len(v2_sub(pos, c->target));
    *vol *= fmaxf(0.25f, 1.0f - d / 45.0f);
}

static void at(const sim_t *s, sfx_t id, int priority, float vol, vec2 pos)
{
    float pan;
    place(s, pos, &vol, &pan);
    play(id, priority, vol, pan);
}

void sound_events(const sim_t *s)
{
    // Busy frames (a swarm dying under a barrage) would churn the channels:
    // a few of each frequent sound per frame is plenty.
    int hits = 0, deaths = 0, shots = 0;
    for (int i = 0; i < s->event_count; i++) {
        const sim_event_t *e = &s->events[i];
        switch (e->type) {
        case EV_SHOT:
            if (shots++ < 4) {
                sfx_t id = e->a == WEAPON_SHOTGUN ? SFX_SHOTGUN : e->a == WEAPON_MACHINE_GUN ? SFX_MG : SFX_RIFLE;
                at(s, id, 1, 0.55f, e->pos);
            }
            break;
        case EV_HIT_ENEMY:
            if (hits++ < 2)
                at(s, (e->a & EV_HIT_ARMORED) ? SFX_ARMOR_HIT : SFX_BUG_HIT, 0, 0.45f, e->pos);
            break;
        case EV_ENEMY_DIED:
            if (deaths++ < 3)
                at(s, SFX_BUG_DIE, 1, 0.6f, e->pos);
            break;
        case EV_PLAYER_HURT:  at(s, SFX_PLAYER_HURT, 2, 0.7f, e->pos); break;
        case EV_PLAYER_DIED:  at(s, SFX_PLAYER_DIE, 4, 0.9f, e->pos); break;
        case EV_DRY_FIRE:     at(s, SFX_DRY_FIRE, 2, 0.6f, e->pos); break;
        case EV_RELOAD:       at(s, SFX_RELOAD, 2, 0.6f, e->pos); break;
        case EV_EXPLOSION:
            at(s, e->a == BLAST_IMPACT ? SFX_IMPACT : SFX_EXPLOSION, 3, e->a == BLAST_SHELL ? 0.8f : 1.0f, e->pos);
            break;
        case EV_CODE_INPUT:    play(SFX_CODE_BEEP, 4, 0.7f, 0.5f); break;
        case EV_CODE_REJECTED: play(SFX_CODE_REJECT, 4, 0.7f, 0.5f); break;
        case EV_BEACON_THROWN: at(s, SFX_BEACON, 3, 0.8f, e->pos); break;
        case EV_PICKUP:        at(s, SFX_PICKUP, 3, 0.8f, e->pos); break;
        case EV_REINFORCED:    play(SFX_RADIO, 3, 0.7f, 0.5f); break;
        case EV_ZONE_CAPTURED: play(SFX_ZONE, 4, 0.9f, 0.5f); break;
        case EV_EXTRACTED:     at(s, SFX_PICKUP, 3, 0.7f, e->pos); break;
        case EV_MISSION:
            if (e->a == MISSION_EXTRACT || e->a == MISSION_BOARDING)
                play(SFX_RADIO, 4, 0.9f, 0.5f);
            if (e->a == MISSION_HOLD)
                sound_music(MUSIC_HOLD);
            else if (e->a == MISSION_SUCCESS)
                sound_music(MUSIC_VICTORY);
            else if (e->a == MISSION_FAILED)
                sound_music(MUSIC_DEFEAT);
            break;
        default:
            break;
        }
    }
    // Things falling from orbit whistle in just before they become visible
    // (latched per beacon: a frame can run two ticks).
    static bool whistled[SIM_MAX_BEACONS];
    for (int i = 0; i < SIM_MAX_BEACONS; i++) {
        const beacon_t *b = &s->beacons[i];
        bool due = b->active && b->state == BEACON_ARMED && b->type != CALLDOWN_EXTRACTION &&
                   b->timer <= DROP_TICKS + 4;
        if (due && !whistled[i])
            at(s, SFX_INCOMING, 3, 0.7f, b->pos);
        whistled[i] = due;
    }
    if (s->waves.number != last_wave) {
        if (s->waves.number > last_wave && s->waves.number > 1)
            play(SFX_ALARM, 2, 0.5f, 0.5f);
        last_wave = s->waves.number;
    }
}
