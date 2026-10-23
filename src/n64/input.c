#include "input.h"

#include <libdragon.h>

static uint16_t to_sim_buttons(joypad_buttons_t b)
{
    uint16_t out = 0;
    if (b.a)       out |= SIM_BTN_A;
    if (b.b)       out |= SIM_BTN_B;
    if (b.z)       out |= SIM_BTN_Z;
    if (b.start)   out |= SIM_BTN_START;
    if (b.d_up)    out |= SIM_BTN_DUP;
    if (b.d_down)  out |= SIM_BTN_DDOWN;
    if (b.d_left)  out |= SIM_BTN_DLEFT;
    if (b.d_right) out |= SIM_BTN_DRIGHT;
    if (b.l)       out |= SIM_BTN_L;
    if (b.r)       out |= SIM_BTN_R;
    if (b.c_up)    out |= SIM_BTN_CUP;
    if (b.c_down)  out |= SIM_BTN_CDOWN;
    if (b.c_left)  out |= SIM_BTN_CLEFT;
    if (b.c_right) out |= SIM_BTN_CRIGHT;
    return out;
}

void input_init(void)
{
    joypad_init();
}

void input_poll(input_frame_t *out)
{
    joypad_poll();
    out->toggle_overlay = false;

    JOYPAD_PORT_FOREACH(port) {
        sim_input_t *in = &out->ports[port];
        in->connected = joypad_is_connected(port);
        if (!in->connected) {
            *in = (sim_input_t){0};
            continue;
        }
        joypad_inputs_t inputs = joypad_get_inputs(port);
        in->held = to_sim_buttons(inputs.btn);
        in->pressed = to_sim_buttons(joypad_get_buttons_pressed(port));
        in->stick_x = inputs.stick_x;
        in->stick_y = inputs.stick_y;

        if ((in->held & SIM_BTN_L) && (in->pressed & SIM_BTN_DUP))
            out->toggle_overlay = true;
    }
}
