#ifndef N64_FRONTEND_H
#define N64_FRONTEND_H

// The screens around the game: drawing for each, the mission briefing's
// names and tips, and menu navigation. The flow between screens lives in
// main.c.

#include <stdbool.h>
#include <stdint.h>

#include "hiscore.h"
#include "input.h"
#include "mission.h"
#include "save.h"

typedef enum {
    SCREEN_SPLASH,    // credit card, then the title
    SCREEN_TITLE,     // the logo over the attract demo
    SCREEN_MENU,      // deploy, high scores, options, credits
    SCREEN_OPTIONS,
    SCREEN_CREDITS,
    SCREEN_SCORES,    // the table: from the menu, the attract loop, or after a mission
    SCREEN_BRIEFING,  // loading screen: operation, objectives, squad
    SCREEN_MISSION,   // the game, its pause screen and results
    SCREEN_INITIALS,  // a new high score
} screen_t;

// Menu input this frame from any controller in `ports` (bit per port).
// The stick and D-pad repeat when held.
typedef struct {
    bool up, down, left, right;
    bool accept;  // A or START
    bool back;    // B
    bool start;
    int  port;    // who pressed accept/back/start (-1 if nobody)
} nav_t;
nav_t frontend_nav(const input_frame_t *in, uint8_t ports);

enum { MENU_DEPLOY, MENU_SCORES, MENU_OPTIONS, MENU_CREDITS, MENU_COUNT };
enum { OPT_MUSIC, OPT_SFX, OPT_BOTS, OPT_ERASE, OPT_BACK, OPT_COUNT };

typedef struct {
    char operation[32];
    char planet[20];
    int  tip;
} briefing_t;
void briefing_make(briefing_t *b, uint32_t seed);

void frontend_loading(float frac);
void frontend_splash(uint32_t frames);
void frontend_title(uint32_t frames, const hiscore_table_t *t, bool saving);
void frontend_menu(int cursor, uint32_t frames);
// erase: frames A has been held on "erase high scores" (0..ERASE_HOLD).
void frontend_options(int cursor, const settings_t *s, int erase);
void frontend_credits(void);
// highlight: rank to blink, or -1. footer: what START/B do here.
void frontend_scores(const hiscore_table_t *t, int highlight, uint32_t frames, const char *footer);
// The squad, objective and difficulty come from the mission already
// generated in `sim`.
void frontend_briefing(const briefing_t *b, const sim_t *sim, uint8_t connected, uint32_t frames);
void frontend_initials(const char initials[3], int pos, uint32_t score, int rank, uint32_t frames);

#define ERASE_HOLD 45   // frames to hold A to erase the high scores

#endif
