#include "difficulty.h"

const difficulty_def_t difficulty_defs[DIFFICULTY_COUNT] = {
    //  name           size off cad  dmg reinf zones nests stages group score
    { "RECON",          70,  0, 130,  70,  7,    2,    3,    2,    2,   75 },
    { "STANDARD",      100,  0, 100, 100, REINFORCE_POOL, 2, 3, 3, 3, 100 },
    { "HAZARD",        120,  1,  90, 115,  5,    2,    4,    3,    3,  130 },
    { "SEVERE",        140,  2,  80, 130,  4,    3,    4,    4,    4,  170 },
    { "HIVEBREAKER",   170,  3,  70, 150,  3,    3,    4,    4,    5,  220 },
};

const difficulty_def_t *sim_difficulty(const sim_t *s)
{
    int d = s->mission.difficulty;
    return &difficulty_defs[d < DIFFICULTY_COUNT ? d : DIFFICULTY_STANDARD];
}

int difficulty_sites(int difficulty, objective_type_t objective)
{
    const difficulty_def_t *d = &difficulty_defs[difficulty < DIFFICULTY_COUNT ? difficulty : DIFFICULTY_STANDARD];
    switch (objective) {
    case OBJ_NESTS:  return d->nests;
    case OBJ_UPLOAD: return 1;
    default:         return d->zones;
    }
}

const char *objective_name(objective_type_t objective)
{
    switch (objective) {
    case OBJ_NESTS:  return "DESTROY THE NESTS";
    case OBJ_UPLOAD: return "UPLOAD THE DATA";
    default:         return "HOLD THE ZONES";
    }
}
