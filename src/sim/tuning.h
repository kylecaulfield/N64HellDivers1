#ifndef SIM_TUNING_H
#define SIM_TUNING_H

// Gameplay tuning. Times are in sim ticks (SIM_HZ per second), distances in
// metres, speeds in metres per second.

#define SIM_HZ 30
#define SIM_DT (1.0f / SIM_HZ)
#define SECONDS(s) ((int)((s) * SIM_HZ + 0.5f))

// --- Pools (compile-time maxima; the active enemy cap is a runtime setting) ---
#define SIM_MAX_ENEMIES      256
#define SIM_ENEMY_CAP_DEFAULT 64
#define SIM_MAX_PROJECTILES  256
#define SIM_MAX_OBSTACLES    32
#define SIM_MAX_EVENTS       64

// --- Arena ---
#define ARENA_HALF_SIZE      32.0f   // arena is a 64x64 m square centred on the origin
#define ARENA_ROCK_COUNT     14
#define ARENA_CLEAR_RADIUS   7.0f    // keep the drop zone free of rocks

// --- Controls ---
#define STICK_DEADZONE       10      // raw units (N64 stick reaches about +-85)
#define STICK_FULL           75      // raw magnitude that counts as full tilt
#define FACE_MIN_MOVE        0.25f   // stick magnitude needed to turn while moving
#define HOLD_B_SWITCH        SECONDS(0.4f) // hold B this long to switch weapon; tap = reload

// Aim assist: half-angle of the cone (radians), max range, and how far the
// shot bends towards the target (0 = none, 1 = snap).
#define AIM_A_CONE           0.17f   // ~10 degrees: stick aiming is precise
#define AIM_A_STRENGTH       0.6f
#define AIM_B_CONE           0.40f   // ~23 degrees: covers half an 8-way sector
#define AIM_B_STRENGTH       1.0f
#define AIM_RANGE            22.0f

// --- Player ---
#define PLAYER_RADIUS        0.4f
#define PLAYER_HP            100.0f
#define PLAYER_SPEED         4.5f
#define PLAYER_STRAFE_SPEED  3.5f    // while firing / facing locked
#define PLAYER_DIVE_SPEED    8.0f
#define PLAYER_DIVE_TICKS    SECONDS(0.4f)
#define PLAYER_PRONE_TICKS   SECONDS(0.35f)
#define PLAYER_DIVE_COOLDOWN SECONDS(1.0f)
#define PLAYER_RESPAWN_TICKS SECONDS(3.0f)  // placeholder until Reinforce (M3)
#define PLAYER_SWITCH_TICKS  SECONDS(0.3f)
#define MUZZLE_OFFSET        0.6f

// --- Enemies: Gnasher (standard warrior) ---
#define GNASHER_RADIUS       0.55f
#define GNASHER_HP           60.0f
#define GNASHER_SPEED        3.2f
#define GNASHER_MELEE_RANGE  1.3f    // centre-to-centre distance that starts an attack
#define GNASHER_MELEE_REACH  1.7f    // still in range when the blow lands -> hit
#define GNASHER_WINDUP       SECONDS(0.4f)
#define GNASHER_RECOVER      SECONDS(0.6f)
#define GNASHER_DAMAGE       25.0f

#define ENEMY_SEPARATION     0.9f    // how strongly overlapping enemies push apart (0..1)
#define TRICKLE_INTERVAL     SECONDS(2.0f)
#define TRICKLE_MAX_ALIVE    6
#define SPAWN_RING_MIN       16.0f
#define SPAWN_RING_MAX       22.0f

// --- Camera (shared, fixed angle) ---
#define CAM_PITCH_RAD        0.9599f // 55 degrees below horizontal
#define CAM_FOV_Y_RAD        0.7854f // 45 degrees vertical
#define CAM_ASPECT           (320.0f / 240.0f)
#define CAM_DIST_MIN         14.0f
#define CAM_DIST_MAX         30.0f
#define CAM_MARGIN           3.0f    // keep players this far inside the visible ground
#define CAM_SMOOTH           0.15f   // per-tick approach factor

#endif
