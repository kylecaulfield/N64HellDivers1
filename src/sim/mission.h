#ifndef SIM_MISSION_H
#define SIM_MISSION_H

// The mission loop:
//
//   DROP       players ride pods down onto the drop zone (crushing whatever
//              is there); waves start once they have landed
//   OBJECTIVE  one of (objective_type_t, picked before the mission):
//              ZONES   hold every zone: a zone fills while a living player
//                      stands in it with no bugs inside, and never empties
//              NESTS   destroy every nest: nests take damage from bullets
//                      (half) and explosions, and send out groups of bugs
//                      once a trooper comes near or one is hurt
//              UPLOAD  at the terminal, enter its code (hold R, C-buttons)
//                      from inside the ring, then keep someone in the ring
//                      while the stage uploads (waves come faster); a new
//                      code for each stage
//   EXTRACT    call the Extraction calldown from inside the LZ
//   HOLD       survive while the waves come faster, until the dropship lands
//   BOARDING   living players who reach the LZ board; once nobody is left
//              alive on the ground, the mission is a success
//   SUCCESS / FAILED   the world stops; the N64 layer shows the results.
//              FAILED is the squad lost (no one alive, no reinforcements).
//
// The objective's sites (zones, nests, the terminal) are arena.sites; how
// many depends on the objective and the difficulty (difficulty.h).

#include "sim.h"

void mission_init(sim_t *s);
void mission_update(sim_t *s);
bool mission_over(const sim_t *s);
// Ticks a zone takes to capture / the hold lasts / an upload stage takes,
// and a nest's hit points (all shorter in quick mode).
int   mission_capture_ticks(const sim_t *s);
int   mission_hold_ticks(const sim_t *s);
int   mission_stage_ticks(const sim_t *s);
float mission_nest_hp(const sim_t *s);
// Objective parts done (zones held, nests destroyed, stages uploaded), and
// how many there are in `total` (may be NULL).
int  mission_parts(const sim_t *s, int *total);
// Player `idx` stands in the LZ while extraction can be called.
bool mission_can_call_extraction(const sim_t *s, int idx);
// The Extraction calldown arrived: start the hold.
void mission_start_extraction(sim_t *s);
// Debug: complete the objective now.
void mission_complete_objective(sim_t *s);
// The squad's stats: every port's added up.
player_stats_t mission_squad_stats(const mission_stats_t *st);

// Nest `n` is there to be shot (OBJ_NESTS, not destroyed).
bool mission_nest_live(const sim_t *s, int n);
// Damage nest `n` (bullets pass their already-reduced damage).
void mission_damage_nest(sim_t *s, int n, float amount, vec2 at);
// Player `idx` can enter the terminal's code now: in the ring, nothing
// uploading, stages left.
bool mission_terminal_open(const sim_t *s, int idx);
// Does the terminal's code start with `inputs` (and can `idx` enter it now)?
bool mission_terminal_prefix(const sim_t *s, int idx, const uint8_t *inputs, int len);
// The terminal code was entered: this stage starts uploading.
void mission_upload_start(sim_t *s, int idx);
// A stage is uploading (the waves come faster).
bool mission_upload_running(const sim_t *s);
// Push a trooper out of the objective's solid structures (live nests, the
// terminal).
void mission_resolve(const sim_t *s, vec2 *pos, float radius);

// The mission's score and where it came from (see SCORE_* in tuning.h).
typedef struct {
    int32_t kills;
    int32_t objective;       // zones held, nests destroyed or stages uploaded
    int32_t extraction;      // the dropship got away, plus each trooper aboard
    int32_t time;            // success only: under par
    int32_t reinforcements;  // negative
    int32_t multiplier;      // percent, from the difficulty
    int32_t total;           // the sum times the multiplier, never below 0
} mission_score_t;
mission_score_t mission_score(const sim_t *s);

#endif
