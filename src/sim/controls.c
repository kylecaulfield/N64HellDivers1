#include "controls.h"

vec2 controls_stick(int8_t x, int8_t y)
{
    vec2 raw = v2((float)x, (float)y);
    float len = v2_len(raw);
    if (len <= (float)STICK_DEADZONE)
        return v2(0.0f, 0.0f);
    float mag = clampf((len - STICK_DEADZONE) / (float)(STICK_FULL - STICK_DEADZONE), 0.0f, 1.0f);
    return v2_scale(raw, mag / len);
}

vec2 controls_cbuttons_dir(uint16_t held)
{
    float x = 0.0f, y = 0.0f;
    if (held & SIM_BTN_CRIGHT) x += 1.0f;
    if (held & SIM_BTN_CLEFT)  x -= 1.0f;
    if (held & SIM_BTN_CUP)    y += 1.0f;
    if (held & SIM_BTN_CDOWN)  y -= 1.0f;
    return v2_norm_or(v2(x, y), v2(0.0f, 0.0f));
}

// B: tap (released before the hold threshold) = reload; hold = switch weapon,
// triggered once as soon as the threshold is reached.
static void read_b(const sim_input_t *in, uint16_t *b_held, player_intent_t *out)
{
    if (in->held & SIM_BTN_B) {
        if (*b_held < UINT16_MAX)
            (*b_held)++;
        if (*b_held == HOLD_B_SWITCH)
            out->switch_weapon = true;
    } else {
        if (*b_held > 0 && *b_held < HOLD_B_SWITCH)
            out->reload = true;
        *b_held = 0;
    }
}

player_intent_t controls_read(control_scheme_t scheme, const sim_input_t *in, uint16_t *b_held)
{
    player_intent_t out = {0};
    if (!in->connected) {
        *b_held = 0;
        return out;
    }

    out.move = controls_stick(in->stick_x, in->stick_y);
    out.code_mode = (in->held & SIM_BTN_R) != 0;
    read_b(in, b_held, &out);

    if (scheme == SCHEME_A) {
        bool z = (in->held & SIM_BTN_Z) != 0;
        out.lock_facing = z;
        out.fire = z && !out.code_mode;
        out.dive = (in->pressed & SIM_BTN_A) != 0;
    } else {
        // In code mode the C-buttons belong to calldown codes, not aiming.
        vec2 c = out.code_mode ? v2(0.0f, 0.0f) : controls_cbuttons_dir(in->held);
        if (v2_len2(c) > 0.0f) {
            out.aim = c;
            out.fire = true;
            out.lock_facing = true;
        }
        out.dive = (in->pressed & SIM_BTN_Z) != 0;
    }
    return out;
}
