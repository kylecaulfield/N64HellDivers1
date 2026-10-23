#include "overlay.h"
#include "gpu_timing.h"
#include "perfwin.h"
#include "version.h"

#include <libdragon.h>
#include <stdio.h>

#define FONT          FONT_BUILTIN_DEBUG_MONO
#define STYLE_NORMAL  0
#define STYLE_WARN    1
#define STYLE_TITLE   2
#define LOG_PERIOD    30 // frames between USB stats lines (~1 s)

static struct {
    bool visible;
    perfwin_t frame, cpu, sim, rdp, gpu;
    sim_counts_t counts;
    uint32_t tick, dropped;
    uint32_t frames;
    int heap_baseline;   // -1 until set
    int heap_growth;     // bytes above baseline, sticky max
} ov;

typedef struct {
    int used_kb;   // everything in use: code, data, stack, heap
    int total_kb;  // installed RDRAM
    int heap_kb;   // heap bytes in use
    int heap_used; // same, in bytes
} mem_stats_t;

static mem_stats_t mem_stats(void)
{
    heap_stats_t h;
    sys_get_heap_stats(&h);
    int total = get_memory_size();
    // Static data, code and stack are whatever the heap does not cover.
    int used = total - h.total + h.used;
    return (mem_stats_t){
        .used_kb = used / 1024,
        .total_kb = total / 1024,
        .heap_kb = h.used / 1024,
        .heap_used = h.used,
    };
}

static void fmt_ms(char *buf, size_t n, uint32_t us)
{
    if (us == GPU_TIMING_NA)
        snprintf(buf, n, "n/a");
    else
        snprintf(buf, n, "%lu.%02lums", (unsigned long)(us / 1000), (unsigned long)(us % 1000) / 10);
}

void overlay_init(void)
{
    rdpq_font_t *font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    rdpq_font_style(font, STYLE_WARN, &(rdpq_fontstyle_t){ .color = RGBA32(0xFF, 0x50, 0x40, 0xFF) });
    rdpq_font_style(font, STYLE_TITLE, &(rdpq_fontstyle_t){ .color = RGBA32(0xE8, 0x96, 0x28, 0xFF) });
    rdpq_text_register_font(FONT, font);
    ov.heap_baseline = -1;
    perfwin_reset(&ov.frame);
    perfwin_reset(&ov.cpu);
    perfwin_reset(&ov.sim);
    perfwin_reset(&ov.rdp);
    perfwin_reset(&ov.gpu);
}

void overlay_toggle(void)
{
    ov.visible = !ov.visible;
    debugf("overlay %s\n", ov.visible ? "on" : "off");
}

void overlay_set_heap_baseline(void)
{
    ov.heap_baseline = mem_stats().heap_used;
    ov.heap_growth = 0;
    debugf("heap baseline: %d bytes\n", ov.heap_baseline);
}

static void log_stats(void)
{
    mem_stats_t m = mem_stats();
    char rdp[16], gpu[16];
    fmt_ms(rdp, sizeof(rdp), ov.rdp.count ? perfwin_avg(&ov.rdp) : GPU_TIMING_NA);
    fmt_ms(gpu, sizeof(gpu), ov.gpu.count ? perfwin_avg(&ov.gpu) : GPU_TIMING_NA);
    uint32_t fps100 = perfwin_avg(&ov.frame) ? 100000000u / perfwin_avg(&ov.frame) : 0;
    debugf("STATS t=%lu fps=%lu.%02lu frame_avg=%luus frame_max=%luus cpu_avg=%luus cpu_max=%luus "
           "sim_avg=%luus rdp=%s gpu_done=%s drop=%lu ent=p%u/e%u/b%u/fx%u/cd%u ram=%dK/%dK heap=%dK%s\n",
           (unsigned long)ov.tick,
           (unsigned long)(fps100 / 100), (unsigned long)(fps100 % 100),
           (unsigned long)perfwin_avg(&ov.frame), (unsigned long)perfwin_max(&ov.frame),
           (unsigned long)perfwin_avg(&ov.cpu), (unsigned long)perfwin_max(&ov.cpu),
           (unsigned long)perfwin_avg(&ov.sim), rdp, gpu, (unsigned long)ov.dropped,
           ov.counts.players, ov.counts.enemies, ov.counts.projectiles,
           ov.counts.particles, ov.counts.calldowns,
           m.used_kb, m.total_kb, m.heap_kb,
           ov.heap_growth > 0 ? " HEAP-GREW" : "");
}

void overlay_record(const frame_timing_t *t, const sim_t *sim, const tickclock_t *clock)
{
    perfwin_push(&ov.frame, t->frame_us);
    perfwin_push(&ov.cpu, t->cpu_us);
    perfwin_push(&ov.sim, t->sim_us);
    uint32_t rdp = gpu_timing_rdp_us(), gpu = gpu_timing_gpu_us();
    if (rdp != GPU_TIMING_NA) perfwin_push(&ov.rdp, rdp);
    if (gpu != GPU_TIMING_NA) perfwin_push(&ov.gpu, gpu);
    ov.counts = sim->counts;
    ov.tick = sim->tick;
    ov.dropped = clock->dropped_ticks;

    if (++ov.frames % LOG_PERIOD == 0) {
        if (ov.heap_baseline >= 0) {
            int growth = mem_stats().heap_used - ov.heap_baseline;
            if (growth > ov.heap_growth) {
                ov.heap_growth = growth;
                debugf("WARN heap grew %d bytes since baseline (malloc during gameplay?)\n", growth);
            }
        }
        log_stats();
    }
}

void overlay_draw(void)
{
    if (!ov.visible)
        return;

    const int x = 12, y0 = 14, lh = 10;
    int y = y0;
    char a[16], b[16], c[16];

    // Opaque backing box in fill mode: the cheapest way to keep text readable.
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 0xFF));
    rdpq_fill_rectangle(x - 4, y0 - 10, x + 216, y0 + 7 * lh - 4);

    mem_stats_t m = mem_stats();
    uint32_t favg = perfwin_avg(&ov.frame);
    uint32_t fps100 = favg ? 100000000u / favg : 0;

    rdpq_text_printf(&(rdpq_textparms_t){ .style_id = STYLE_TITLE }, FONT, x, y,
                     "HIVEBREAKERS %s", GAME_VERSION);
    y += lh;
    fmt_ms(a, sizeof(a), favg);
    fmt_ms(b, sizeof(b), perfwin_max(&ov.frame));
    rdpq_text_printf(&(rdpq_textparms_t){ .style_id = fps100 < 2950 ? STYLE_WARN : STYLE_NORMAL }, FONT, x, y,
                     "FPS %lu.%02lu frame %s max %s",
                     (unsigned long)(fps100 / 100), (unsigned long)(fps100 % 100), a, b);
    y += lh;
    fmt_ms(a, sizeof(a), perfwin_avg(&ov.cpu));
    fmt_ms(b, sizeof(b), perfwin_max(&ov.cpu));
    fmt_ms(c, sizeof(c), perfwin_avg(&ov.sim));
    rdpq_text_printf(NULL, FONT, x, y, "CPU %s max %s sim %s", a, b, c);
    y += lh;
    fmt_ms(a, sizeof(a), ov.rdp.count ? perfwin_avg(&ov.rdp) : GPU_TIMING_NA);
    fmt_ms(b, sizeof(b), ov.gpu.count ? perfwin_avg(&ov.gpu) : GPU_TIMING_NA);
    rdpq_text_printf(NULL, FONT, x, y, "RDP %s  GPU done %s", a, b);
    y += lh;
    rdpq_text_printf(&(rdpq_textparms_t){ .style_id = ov.dropped ? STYLE_WARN : STYLE_NORMAL }, FONT, x, y,
                     "TICK %lu dropped %lu", (unsigned long)ov.tick, (unsigned long)ov.dropped);
    y += lh;
    rdpq_text_printf(NULL, FONT, x, y, "ENT P%u E%u B%u FX%u CD%u",
                     ov.counts.players, ov.counts.enemies, ov.counts.projectiles,
                     ov.counts.particles, ov.counts.calldowns);
    y += lh;
    rdpq_text_printf(&(rdpq_textparms_t){ .style_id = ov.heap_growth > 0 ? STYLE_WARN : STYLE_NORMAL }, FONT, x, y,
                     "RAM %d/%dK heap %dK%s", m.used_kb, m.total_kb, m.heap_kb,
                     ov.heap_growth > 0 ? " GREW!" : "");
}
