#ifndef N64_SOUND_H
#define N64_SOUND_H

// Music and sound effects (libdragon's mixer, xm64 and wav64). Everything is
// loaded at boot, so nothing allocates once the game is running.
//
// Gameplay sounds come from the sim's events, panned and attenuated by where
// they happen relative to the camera. The music follows the screen and the
// mission: title, mission, the extraction hold, then a victory or defeat
// sting.

#include <stdbool.h>

#include "sim.h"

typedef enum {
    MUSIC_NONE = -1,
    MUSIC_TITLE,
    MUSIC_MISSION,
    MUSIC_HOLD,
    MUSIC_VICTORY,
    MUSIC_DEFEAT,
    MUSIC_COUNT
} music_t;

typedef enum { UI_MOVE, UI_SELECT, UI_BACK, UI_JOIN, UI_ERROR } ui_sound_t;

// `progress` (may be NULL) is called after each file loads, for the loading
// screen; `total` is how many calls to expect.
void sound_init(void (*progress)(int done, int total));
int  sound_file_count(void);
// Feed the audio hardware. Call a few times per frame (at least once).
void sound_update(void);
// Switch the music (no-op if it is already playing).
void sound_music(music_t m);
// Sound effects and music cues for every event the sim produced since the
// last call.
void sound_events(const sim_t *s);
void sound_ui(ui_sound_t u);
// 0..10 each.
void sound_set_volumes(int music, int sfx);
// Lower the music while paused.
void sound_duck(bool on);

#endif
