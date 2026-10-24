#ifndef N64_UI_H
#define N64_UI_H

// 2D drawing shared by the front-end screens: the heading font (tools/
// genart.py's pixel face, tinted by the text styles of overlay.h), the logo
// and planet sprites, panels, fades and a starfield.

#include <stdbool.h>
#include <stdint.h>

#define FONT_HEADING 3   // rdpq font id (1 is the debug font)
#define HEADING_H    16  // its glyph height, pixels
#define HEADING_W    12  // its advance

// Loads the font and sprites. Needs the filesystem and rdpq.
void ui_init(void);
// The logo centred on x, top at y, scaled (1 = its real size).
void ui_logo(float cx, float y, float scale);
int  ui_logo_width(void);
void ui_planet(int x, int y, int biome);  // biome_t
// An opaque dark panel with a thin border.
void ui_box(int x0, int y0, int x1, int y1);
// Darken a rectangle (alpha 0..255).
void ui_shade(int x0, int y0, int x1, int y1, uint8_t alpha);
// Cover the whole screen with black, 0..1.
void ui_fade(float amount);
void ui_stars(uint32_t frame);
// A filled disc of radius `r` (fill mode).
void ui_disc(int cx, int cy, int r, const uint8_t rgb[3]);
void ui_progress(int x, int y, int w, float frac);
// Heading-font text, centred across the screen at baseline y.
void ui_heading(int y, int style, const char *text);

#endif
