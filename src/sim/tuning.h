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
#define SIM_MAX_EVENTS       192     // explosions can hit dozens of bugs in one tick
#define SIM_MAX_BEACONS      16      // four players with a full loadout each, plus their bots' calls
#define SIM_MAX_STRIKES      8
#define SIM_MAX_TURRETS      4
#define SIM_MAX_PICKUPS      8
#define SIM_MAX_MINES        24
#define SIM_MAX_FIRES        8

// --- Arena ---
#define ARENA_HALF_SIZE      32.0f   // arena is a 64x64 m square centred on the origin
#define ARENA_ROCK_COUNT     14
#define ARENA_CLEAR_RADIUS   7.0f    // keep the drop zone and the LZ free of rocks
#define ZONE_CLEAR_RADIUS    5.5f    // ...and the objective zones
#define ZONE_DIST_MIN        15.0f   // zones and the LZ sit this far from the drop zone
#define ZONE_DIST_MAX        22.0f
#define SITE_SPACING         15.0f   // and at least this far from each other
#define SITE_SPACING_CROWDED 12.0f   // ...with four or more sites (LZ included)
#define SITE_EDGE            7.0f    // and this far inside the arena edge

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
#define PLAYER_SWITCH_TICKS  SECONDS(0.3f)
#define MUZZLE_OFFSET        0.6f

// --- Enemies ---
// Skitter: fast swarmer, dies to anything.
#define SKITTER_RADIUS       0.35f
#define SKITTER_HP           20.0f
#define SKITTER_SPEED        6.0f
#define SKITTER_MELEE_RANGE  0.9f
#define SKITTER_MELEE_REACH  1.3f
#define SKITTER_WINDUP       SECONDS(0.2f)
#define SKITTER_RECOVER      SECONDS(0.4f)
#define SKITTER_DAMAGE       8.0f

// Gnasher: standard warrior.
#define GNASHER_RADIUS       0.55f
#define GNASHER_HP           60.0f
#define GNASHER_SPEED        3.2f
#define GNASHER_MELEE_RANGE  1.3f    // centre-to-centre distance that starts an attack
#define GNASHER_MELEE_REACH  1.7f    // still in range when the blow lands -> hit
#define GNASHER_WINDUP       SECONDS(0.4f)
#define GNASHER_RECOVER      SECONDS(0.6f)
#define GNASHER_DAMAGE       25.0f

// Bulwark: slow armored brute. Hits on its front arc do a fraction of the
// damage: flank it.
#define BULWARK_RADIUS       1.1f
#define BULWARK_HP           400.0f
#define BULWARK_SPEED        1.8f
#define BULWARK_MELEE_RANGE  2.2f
#define BULWARK_MELEE_REACH  2.8f
#define BULWARK_WINDUP       SECONDS(0.8f)
#define BULWARK_RECOVER      SECONDS(1.0f)
#define BULWARK_DAMAGE       45.0f
#define BULWARK_FRONT_ARMOR  0.2f    // damage multiplier on the front arc
#define FRONT_ARC_COS        0.342f  // front arc = within 70 degrees of facing

#define ENEMY_MAX_RADIUS     BULWARK_RADIUS
#define ENEMY_SEPARATION     0.9f    // how strongly overlapping enemies push apart (0..1)
#define SPAWN_RING_MIN       16.0f
#define SPAWN_RING_MAX       22.0f

// --- Flow field ---
#define FLOW_BUDGET_DEFAULT  512     // cells expanded per tick (a full field is 4096)
#define FLOW_REBUILD_TICKS   6       // a new build starts at most this often (0.2 s)
#define FLOW_LIMIT_MARGIN    16      // builds reach this far past the farthest bug (8 m)
#define FLOW_ROCK_MARGIN     0.5f    // rocks block cells this far beyond their radius
#define FLOW_DIRECT_RANGE    4.0f    // closer than this, bugs head straight for the player

// --- Waves ---
#define WAVE_FIRST_DELAY     SECONDS(3.0f)
#define WAVE_BREAK_TICKS     SECONDS(6.0f)
#define WAVE_GROUP_INTERVAL  SECONDS(1.5f)
#define WAVE_GROUP_SIZE      6
#define WAVE_CLEAR_DIVISOR   5       // next wave once alive <= wave size / 5
#define BREACH_RING_MIN      18.0f   // breaches open this far from the camera target
#define BREACH_RING_MAX      24.0f

// --- Calldowns (codes, cooldowns and call-in delays are in calldown.c) ---
#define REINFORCE_POOL       5       // shared reinforcements per mission
#define BEACON_THROW_SPEED   12.0f
#define BEACON_FLIGHT_TICKS  SECONDS(0.5f)  // so a throw lands ~6 m away
#define DROP_TICKS           SECONDS(0.6f)  // the drop is visible falling for this long
#define DROP_CRUSH_RADIUS    1.3f    // landing on something kills it, teammates included
#define DROP_CRUSH_DAMAGE    500.0f
#define SQUAD_REINFORCE_TICKS SECONDS(5.0f) // whole squad down: emergency reinforce after
#define SQUAD_LOST_TICKS     SECONDS(5.0f)  // down with no reinforcements: restart after
#define EXPLOSION_EDGE       0.5f    // fraction of a blast's damage at its edge

#define AIRSTRIKE_BOMBS      5       // in a line along the thrower's facing
#define AIRSTRIKE_SPACING    3.0f
#define AIRSTRIKE_INTERVAL   3
#define AIRSTRIKE_RADIUS     3.0f
#define AIRSTRIKE_DAMAGE     300.0f

#define BARRAGE_SHELLS       15      // at random inside the area, one every interval
#define BARRAGE_INTERVAL     SECONDS(0.4f)
#define BARRAGE_AREA         8.0f
#define BARRAGE_RADIUS       2.5f
#define BARRAGE_DAMAGE       250.0f

#define SENTRY_RANGE         16.0f
#define SENTRY_AMMO          200
#define SENTRY_LIFE          SECONDS(45.0f)
#define SENTRY_TURN          0.25f   // radians per tick
#define SENTRY_AIM_TOLERANCE 0.12f   // fires when this close to the target's bearing
#define SENTRY_FIRE_INTERVAL 3
#define SENTRY_DAMAGE        15.0f
#define SENTRY_SPEED         60.0f
#define SENTRY_SPREAD        0.03f
#define SENTRY_RETARGET      6       // ticks between target searches

#define PICKUP_RADIUS        1.0f
#define RESUPPLY_USES        4
#define RESUPPLY_LIFE        SECONDS(60.0f)
#define MG_PICKUP_LIFE       SECONDS(90.0f)   // the machine gun's and the launcher's cases

#define MINE_COUNT           8       // scattered within MINE_SPREAD of the beacon
#define MINE_SPREAD          4.5f
#define MINE_TRIGGER         0.9f    // a bug this close sets one off (troopers don't)
#define MINE_RADIUS          2.2f
#define MINE_DAMAGE          220.0f
#define MINE_ARM_TICKS       SECONDS(0.5f)
#define MINE_LIFE            SECONDS(90.0f)

#define LAUNCHER_RADIUS      2.5f    // a grenade's blast
#define LAUNCHER_DAMAGE      160.0f

#define NAPALM_PATCHES       4       // in a line along the throw, like the airstrike
#define NAPALM_SPACING       3.0f
#define NAPALM_INTERVAL      3
#define NAPALM_BURST_RADIUS  1.8f    // each canister's burst...
#define NAPALM_BURST_DAMAGE  80.0f
#define FIRE_RADIUS          2.0f    // ...and the fire it leaves
#define FIRE_LIFE            SECONDS(8.0f)
#define FIRE_DPS             50.0f
#define FIRE_TICK            6       // burns every this many ticks (fewer hit events)

// --- Mission ---
#define DROP_IN_TICKS        SECONDS(2.0f)   // players ride pods down at the start
#define ZONE_RADIUS          4.0f
#define ZONE_CAPTURE_TICKS   SECONDS(30.0f)  // standing in it with no bugs inside
#define LZ_RADIUS            6.0f
#define EXTRACT_HOLD_TICKS   SECONDS(75.0f)  // from the extraction call to touchdown
#define DROPSHIP_DESCENT     SECONDS(4.0f)   // seen coming down for this long before it lands
#define HOLD_WAVE_SPEEDUP    3       // during the hold, wave breaks and group intervals are this much shorter
#define QUICK_MISSION_DIV    5       // debug: quick mission divides capture, nest, upload and hold times

// Nests (OBJ_NESTS)
#define NEST_RADIUS          1.6f    // solid for troopers and bullets; bugs crawl out of it
#define NEST_HP              800.0f
#define NEST_BULLET_MULT     0.5f    // bullets do half damage to a nest; explosions full
#define NEST_WAKE_RADIUS     18.0f   // a trooper this close wakes it (so does hurting it)
#define NEST_SPAWN_TICKS     SECONDS(7.0f)  // between groups once awake (difficulty cadence scales it)
#define NEST_FIRST_TICKS     SECONDS(1.5f)  // first group after waking

// Upload (OBJ_UPLOAD)
#define UPLOAD_RADIUS        6.0f    // enter the code and stay inside this ring
#define UPLOAD_STAGE_TICKS   SECONDS(20.0f) // per stage, while someone is in the ring
#define UPLOAD_CODE_LEN      6
#define UPLOAD_WAVE_SPEEDUP  2       // waves come this much faster while a stage uploads
#define TERMINAL_RADIUS      0.7f    // the console itself is solid

// --- Score ---
#define SCORE_KILL_SKITTER   10
#define SCORE_KILL_GNASHER   25
#define SCORE_KILL_BULWARK   60
#define SCORE_ZONE           500     // per zone held
#define SCORE_NEST           400     // per nest destroyed
#define SCORE_STAGE          400     // per upload stage
#define SCORE_EXTRACTED      2000    // the dropship got away
#define SCORE_ABOARD         500     // per trooper on it
#define SCORE_PAR_SECONDS    600     // a successful mission scores time bonus below this
#define SCORE_PER_SECOND     5       // ...per second under par
#define SCORE_REINFORCEMENT  250     // taken off per reinforcement used

// --- Bots (stress-test stand-ins for players 2-4) ---
#define BOT_ENGAGE_RANGE     18.0f
#define BOT_KITE_RANGE       4.0f
#define BOT_FOLLOW_DIST      3.0f
#define BOT_FF_CLEARANCE     0.6f    // a bot holds fire when a teammate is this close to its line
#define BOT_NEST_STANDOFF    7.0f    // the leading bot shoots a nest from this far
#define BOT_NEST_RANGE       12.0f   // bots shoot nests this close (on screen, not past its edge)
#define BOT_CRATE_RANGE      20.0f   // nearly out of ammo: walk to a resupply crate this close, or call one
#define BOT_WALL_MARGIN      1.5f    // backing off would end this close to the edge: slide sideways
#define BOT_SENTRY_BUGS      8       // a bot drops a sentry when this many bugs are within...
#define BOT_SENTRY_RANGE     12.0f
#define BOT_DROP_CLEARANCE   1.5f    // bots keep this clear of where a drop lands (on top of its crush radius)

// --- Camera (shared, fixed angle) ---
#define CAM_PITCH_RAD        0.9599f // 55 degrees below horizontal
#define CAM_FOV_Y_RAD        0.7854f // 45 degrees vertical
#define CAM_ASPECT           (320.0f / 240.0f)
#define CAM_DIST_MIN         14.0f
#define CAM_DIST_MAX         30.0f
#define CAM_MARGIN           3.0f    // keep players this far inside the visible ground
#define CAM_SMOOTH           0.15f   // per-tick approach factor

#endif
