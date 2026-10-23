#include "boot.h"
#include "version.h"

#include <libdragon.h>

void boot_require_expansion_pak(void)
{
    if (is_memory_expanded())
        return;

    debugf("FATAL: Expansion Pak not detected (%d KiB RDRAM). Halting.\n",
           get_memory_size() / 1024);

    // Keep this path tiny: 2 framebuffers, the built-in font, nothing loaded
    // from ROM. It has to work in 4 MiB.
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    for (;;) {
        rdpq_attach_clear(display_get(), NULL);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 70, "HIVEBREAKERS");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 100, "EXPANSION PAK REQUIRED");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 125, "This game needs the N64 Expansion");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 137, "Pak (8 MB). Power off the console,");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 149, "insert it in place of the Jumper");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 161, "Pak, then power on again.");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 190, "Detected RDRAM: %d KB", get_memory_size() / 1024);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 202, "Build %s", GAME_VERSION);
        rdpq_detach_show();
    }
}
