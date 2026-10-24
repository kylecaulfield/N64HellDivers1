#include "controls.h"
#include "test.h"

static sim_input_t pad(uint16_t held, uint16_t pressed, int8_t sx, int8_t sy)
{
    return (sim_input_t){ .connected = true, .held = held, .pressed = pressed, .stick_x = sx, .stick_y = sy };
}

void test_controls_stick_deadzone_and_scaling(void)
{
    vec2 v = controls_stick(5, -6);
    CHECK_NEAR(v.x, 0.0f, 1e-6);
    CHECK_NEAR(v.y, 0.0f, 1e-6);

    v = controls_stick(85, 0);
    CHECK_NEAR(v.x, 1.0f, 1e-5);
    CHECK_NEAR(v.y, 0.0f, 1e-5);

    v = controls_stick(0, -85);
    CHECK_NEAR(v.y, -1.0f, 1e-5);

    // Half way between the deadzone and full tilt.
    v = controls_stick((int8_t)((STICK_DEADZONE + STICK_FULL) / 2), 0);
    CHECK(v.x > 0.4f && v.x < 0.6f);
}

void test_controls_cbuttons_eight_directions(void)
{
    vec2 v = controls_cbuttons_dir(SIM_BTN_CUP);
    CHECK_NEAR(v.x, 0.0f, 1e-6);
    CHECK_NEAR(v.y, 1.0f, 1e-6);

    v = controls_cbuttons_dir(SIM_BTN_CDOWN | SIM_BTN_CLEFT);
    CHECK_NEAR(v.x, -0.7071f, 1e-3);
    CHECK_NEAR(v.y, -0.7071f, 1e-3);

    v = controls_cbuttons_dir(SIM_BTN_CUP | SIM_BTN_CRIGHT);
    CHECK_NEAR(v.x, 0.7071f, 1e-3);
    CHECK_NEAR(v.y, 0.7071f, 1e-3);

    // Opposing buttons cancel; nothing held is no direction.
    v = controls_cbuttons_dir(SIM_BTN_CUP | SIM_BTN_CDOWN);
    CHECK_NEAR(v2_len(v), 0.0f, 1e-6);
    v = controls_cbuttons_dir(SIM_BTN_CUP | SIM_BTN_CDOWN | SIM_BTN_CRIGHT);
    CHECK_NEAR(v.x, 1.0f, 1e-6);
    CHECK_NEAR(v2_len(controls_cbuttons_dir(0)), 0.0f, 1e-6);
}

void test_controls_scheme_a(void)
{
    uint16_t b = 0;
    sim_input_t in = pad(SIM_BTN_Z, 0, 80, 0);
    player_intent_t it = controls_read(SCHEME_A, &in, &b);
    CHECK(it.fire);
    CHECK(it.lock_facing);
    CHECK(it.move.x > 0.9f);
    CHECK_NEAR(v2_len(it.aim), 0.0f, 1e-6); // scheme A aims with the facing

    in = pad(0, SIM_BTN_A, 0, 0);
    it = controls_read(SCHEME_A, &in, &b);
    CHECK(it.dive);
    CHECK(!it.fire);

    // Holding R: code mode, no firing (but Z still holds the facing).
    in = pad(SIM_BTN_Z | SIM_BTN_R, 0, 0, 0);
    it = controls_read(SCHEME_A, &in, &b);
    CHECK(it.code_mode);
    CHECK(!it.fire);
    CHECK(it.lock_facing);

    // Z is not dive in scheme A, C-buttons do nothing.
    in = pad(SIM_BTN_CUP, SIM_BTN_Z, 0, 0);
    it = controls_read(SCHEME_A, &in, &b);
    CHECK(!it.dive);
    CHECK(!it.fire);
}

void test_controls_scheme_b(void)
{
    uint16_t b = 0;
    sim_input_t in = pad(SIM_BTN_CLEFT, 0, 0, 80);
    player_intent_t it = controls_read(SCHEME_B, &in, &b);
    CHECK(it.fire);
    CHECK(it.lock_facing);
    CHECK_NEAR(it.aim.x, -1.0f, 1e-6);
    CHECK(it.move.y > 0.9f); // moving one way while shooting another

    in = pad(0, SIM_BTN_Z, 0, 0);
    it = controls_read(SCHEME_B, &in, &b);
    CHECK(it.dive);

    in = pad(0, SIM_BTN_A, 0, 0);
    it = controls_read(SCHEME_B, &in, &b);
    CHECK(!it.dive);

    // No C held: no fire, facing follows movement.
    in = pad(0, 0, 80, 0);
    it = controls_read(SCHEME_B, &in, &b);
    CHECK(!it.fire);
    CHECK(!it.lock_facing);

    // R held: the C-buttons are for calldown codes, not shooting.
    in = pad(SIM_BTN_R | SIM_BTN_CUP, 0, 0, 0);
    it = controls_read(SCHEME_B, &in, &b);
    CHECK(it.code_mode);
    CHECK(!it.fire);
    CHECK_NEAR(v2_len(it.aim), 0.0f, 1e-6);
}

void test_controls_b_tap_reloads(void)
{
    uint16_t b = 0;
    sim_input_t held = pad(SIM_BTN_B, 0, 0, 0), up = pad(0, 0, 0, 0);
    for (int i = 0; i < 3; i++) {
        player_intent_t it = controls_read(SCHEME_A, &held, &b);
        CHECK(!it.reload);
        CHECK(!it.switch_weapon);
    }
    player_intent_t it = controls_read(SCHEME_A, &up, &b);
    CHECK(it.reload);
    CHECK(!it.switch_weapon);
    CHECK_EQ_INT(b, 0);
}

void test_controls_b_hold_switches_once(void)
{
    uint16_t b = 0;
    sim_input_t held = pad(SIM_BTN_B, 0, 0, 0), up = pad(0, 0, 0, 0);
    int switches = 0;
    for (int i = 0; i < HOLD_B_SWITCH * 3; i++) {
        player_intent_t it = controls_read(SCHEME_B, &held, &b);
        CHECK(!it.reload);
        if (it.switch_weapon)
            switches++;
    }
    CHECK_EQ_INT(switches, 1);
    player_intent_t it = controls_read(SCHEME_B, &up, &b);
    CHECK(!it.reload); // releasing after a hold is not a reload
}

void test_controls_disconnected_is_idle(void)
{
    uint16_t b = 5;
    sim_input_t in = { .connected = false, .held = SIM_BTN_Z, .stick_x = 80 };
    player_intent_t it = controls_read(SCHEME_A, &in, &b);
    CHECK(!it.fire);
    CHECK_NEAR(v2_len(it.move), 0.0f, 1e-6);
    CHECK_EQ_INT(b, 0);
}
