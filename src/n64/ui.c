#include "ui.h"

#include <libdragon.h>

#include "overlay.h"
#include "palette.h"

static sprite_t *logo, *planet;

void ui_init(void)
{
    rdpq_font_t *font = rdpq_font_load("rom:/hive.font64");
    // The same style ids as the debug font (overlay.h), so text code can use
    // either font with the same colours.
    static const color_t STYLES[] = {
        [STYLE_NORMAL] = RGBA32(0xF0, 0xF0, 0xF0, 0xFF), [STYLE_WARN] = RGBA32(0xFF, 0x50, 0x40, 0xFF),
        [STYLE_TITLE] = RGBA32(0xE8, 0x96, 0x28, 0xFF),  [STYLE_DIM] = RGBA32(0x80, 0x80, 0x88, 0xFF),
        [STYLE_GOOD] = RGBA32(0x70, 0xF0, 0x80, 0xFF),   [STYLE_ZONE_B] = RGBA32(0x50, 0xDC, 0xFF, 0xFF),
        [STYLE_ZONE_C] = RGBA32(0xC8, 0x78, 0xFF, 0xFF),
    };
    for (int i = 0; i < STYLE_P1; i++)
        rdpq_font_style(font, (uint8_t)i, &(rdpq_fontstyle_t){ .color = STYLES[i] });
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const uint8_t *c = PLAYER_RGB[i];
        rdpq_font_style(font, (uint8_t)(STYLE_P1 + i), &(rdpq_fontstyle_t){ .color = RGBA32(c[0], c[1], c[2], 0xFF) });
    }
    rdpq_text_register_font(FONT_HEADING, font);
    logo = sprite_load("rom:/logo.sprite");
    planet = sprite_load("rom:/planet.sprite");
}

int ui_logo_width(void)
{
    return logo->width;
}

void ui_logo(float cx, float y, float scale)
{
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_POINT);
    float w = (float)logo->width * scale;
    rdpq_sprite_blit(logo, cx - w * 0.5f, y, &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
}

void ui_planet(int x, int y)
{
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_sprite_blit(planet, (float)x, (float)y, NULL);
}

void ui_box(int x0, int y0, int x1, int y1)
{
    rdpq_set_mode_fill(RGBA32(0xE8, 0x96, 0x28, 0xFF));
    rdpq_fill_rectangle(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    rdpq_set_fill_color(RGBA32(10, 12, 18, 0xFF));
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

void ui_shade(int x0, int y0, int x1, int y1, uint8_t alpha)
{
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

void ui_fade(float amount)
{
    if (amount <= 0.0f)
        return;
    if (amount >= 1.0f) {
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 0xFF));
        rdpq_fill_rectangle(0, 0, 320, 240);
        return;
    }
    ui_shade(0, 0, 320, 240, (uint8_t)(amount * 255.0f));
}

void ui_stars(uint32_t frame)
{
    // Fixed positions, three brightnesses; the brightest twinkle.
    enum { STARS = 90 };
    static int16_t sx[STARS], sy[STARS];
    static uint8_t level[STARS];
    static bool ready;
    if (!ready) {
        uint32_t s = 0x5EED;
        for (int i = 0; i < STARS; i++) {
            s = s * 1664525u + 1013904223u;
            sx[i] = (int16_t)((s >> 8) % 320);
            s = s * 1664525u + 1013904223u;
            sy[i] = (int16_t)((s >> 8) % 240);
            level[i] = (uint8_t)((s >> 20) % 3);
        }
        ready = true;
    }
    static const uint8_t SHADE[3] = { 70, 130, 230 };
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 0xFF));
    for (int l = 0; l < 3; l++) {
        uint8_t v = SHADE[l];
        rdpq_set_fill_color(RGBA32(v, v, (uint8_t)(v + (255 - v) / 4), 0xFF));
        for (int i = 0; i < STARS; i++) {
            if (level[i] != l || (l == 2 && ((frame / 8 + (uint32_t)i) % 13) == 0))
                continue;
            rdpq_fill_rectangle(sx[i], sy[i], sx[i] + 1, sy[i] + 1);
        }
    }
}

void ui_progress(int x, int y, int w, float frac)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    rdpq_set_mode_fill(RGBA32(60, 60, 70, 0xFF));
    rdpq_fill_rectangle(x - 1, y - 1, x + w + 1, y + 7);
    rdpq_set_fill_color(RGBA32(16, 16, 20, 0xFF));
    rdpq_fill_rectangle(x, y, x + w, y + 6);
    rdpq_set_fill_color(RGBA32(0xE8, 0x96, 0x28, 0xFF));
    rdpq_fill_rectangle(x, y, x + (int)((float)w * frac), y + 6);
}

void ui_heading(int y, int style, const char *text)
{
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER, .style_id = (int16_t)style },
                     FONT_HEADING, 0, (float)y, "%s", text);
}
