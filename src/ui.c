#define _GNU_SOURCE
#include "ui.h"
#include "controller_pins.h"
#include "identify.h"
#include "ipc.h"
#include "product_info.h"
#include "settings.h"
#include "shm_data.h"
#include "sysinfo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

/* Normally injected by CMake from debian/changelog; only a build that bypasses
 * our CMakeLists can miss it. */
#ifndef GOCONTROLL_HMI_DEMO_VERSION
#define GOCONTROLL_HMI_DEMO_VERSION "unknown"
#endif

/* Montserrat 14 regenerated locally to include U+00A9 (c) for the main-screen
 * footer — the stock LVGL build only covers ASCII + degree + bullet. The .c
 * source lives in src/fonts/ so we don't patch the lvgl submodule. */
LV_FONT_DECLARE(lv_font_montserrat_14_ext);

/* --- Brand palette (sampled from assets/GOcontroll.png) ---------------- */
#define BRAND_ORANGE        lv_color_hex(0xE94E24)
#define BRAND_DARK          lv_color_hex(0x1C1C1C)
#define BRAND_BG            lv_color_hex(0xF2F2F2)
#define BRAND_PANEL         lv_color_hex(0xFFFFFF)
#define BRAND_SUBTEXT       lv_color_hex(0x6B6B6B)
#define BRAND_DIVIDER       lv_color_hex(0xD9D9D9)
#define BRAND_ORANGE_SOFT   lv_color_hex(0xFBE2D9)

#define TICK_PERIOD_MS  200  /* 5 Hz — responsive slider + live readouts */
#define CHART_HZ        1    /* chart still advances at 1 Hz */
#define CHART_POINTS    60   /* 60 s rolling window at 1 Hz */
#define GAUGE_RANGE_MV  16000 /* 0 - 16 V in mV for arc value */
#define BACKLIGHT_SYSFS "/sys/class/backlight/max25014/brightness"
#define IPC_RESCAN_MS   2000  /* don't walk /proc more often than this */

static struct {
    pid_t            demo_pid;
    uint32_t         ipc_last_scan;   /* lv_tick when we last walked /proc */
    int              ipc_scanned;     /* ipc_last_scan holds a real timestamp */
    hmi_config_t     cfg;
    hmi_settings_t  *settings;
    char             asset_dir[128];
    int              should_exit;

    /* Screens */
    lv_obj_t *scr_main, *scr_info, *scr_live, *scr_settings;

    /* Main */
    lv_obj_t   *lbl_clock;
    lv_timer_t *tick_timer;   /* 1 Hz */

    /* Information screen — dynamic fields (IP/MAC may change with DHCP) */
    lv_obj_t *info_val_ip;
    lv_obj_t *info_val_mac;

    /* Live Data screen */
    lv_obj_t          *arc_k15,  *lbl_k15;
    lv_obj_t          *arc_k30,  *lbl_k30;
    lv_obj_t          *chart_temp;
    lv_chart_series_t *ser_temp;
    lv_obj_t          *lbl_cpu_temp;
    lv_obj_t          *lbl_temperature;
    lv_obj_t          *bar_pressure, *lbl_pressure;
    lv_obj_t          *bar_soc,      *lbl_soc;
    lv_obj_t          *lbl_power;

    /* Settings screen */
    lv_obj_t *slider_bright, *cb_clock;
} g;

/* --- Simulink IPC target ---------------------------------------------- */
/* The pid is looked up once in main(), but the backend is a separate service
 * that can come and go under us. dh_installsystemd restarts the four units in
 * four separate transactions, so the unit's After=gocontroll_demo.service does
 * not order a package upgrade: the HMI regularly restarts first and captures
 * the pid the backend is about to leave behind. Reads then fail with ESRCH
 * forever and both gauges sit at "-" until someone restarts the HMI by hand.
 *
 * So treat the pid as a cache and re-resolve it whenever IPC fails. Rate
 * limited, because "no backend at all" is a documented, permanent state and
 * must not walk /proc at the tick rate. */
static pid_t ipc_target(void)
{
    if (g.demo_pid >= 0) return g.demo_pid;

    if (g.ipc_scanned && lv_tick_elaps(g.ipc_last_scan) < IPC_RESCAN_MS) return -1;
    g.ipc_scanned   = 1;
    g.ipc_last_scan = lv_tick_get();

    g.demo_pid = ipc_find_pid(IPC_PROCESS_NAME);
    return g.demo_pid;
}

/* A failed transfer means the pid is stale (the backend exited) or the address
 * is wrong. Drop the cache either way: the next call re-resolves, and if the
 * pid was fine the rescan simply hands back the same one. */
static void ipc_invalidate(void)
{
    g.demo_pid = -1;
}

/* --- asap scalar read helper ------------------------------------------ */
static int read_u16(const asap_element_t *el, uint16_t *out)
{
    pid_t pid = ipc_target();
    if (pid < 0) return -1;
    size_t n = asap_datatype_size(el->type);
    uint8_t buf[8] = {0};
    if (ipc_read(pid, el->address, buf, n) != (ssize_t)n) {
        ipc_invalidate();
        return -1;
    }
    switch (el->type) {
    case DTYPE_UINT8:
    case DTYPE_INT8:     *out = buf[0];                               return 0;
    case DTYPE_UINT16:
    case DTYPE_INT16:    *out = (uint16_t)(buf[0] | (buf[1] << 8));   return 0;
    default:             *out = 0;                                    return -1;
    }
}

static int32_t read_backlight(void)
{
    FILE *f = fopen("/sys/class/backlight/max25014/brightness", "r");
    if (!f) return 50;
    int v = 50;
    if (fscanf(f, "%d", &v) != 1) v = 50;
    fclose(f);
    if (v < 10) v = 10;
    if (v > 100) v = 100;
    return v;
}

/* --- styling helpers -------------------------------------------------- */
static void style_panel(lv_obj_t *o)
{
    lv_obj_set_style_bg_color(o, BRAND_PANEL, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, BRAND_DIVIDER, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_all(o, 16, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
}

static void style_title(lv_obj_t *lbl)
{
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl, BRAND_DARK, 0);
}

static void style_heading(lv_obj_t *lbl)
{
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, BRAND_DARK, 0);
}

static void style_sub(lv_obj_t *lbl)
{
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, BRAND_SUBTEXT, 0);
}

/* --- navigation ------------------------------------------------------- */
static void go_main_cb    (lv_event_t *e) { (void)e; lv_screen_load(g.scr_main); }
static void go_info_cb    (lv_event_t *e) { (void)e; lv_screen_load(g.scr_info); }
static void go_live_cb    (lv_event_t *e) { (void)e; lv_screen_load(g.scr_live); }
static void go_settings_cb(lv_event_t *e) { (void)e; lv_screen_load(g.scr_settings); }
static void exit_cb       (lv_event_t *e) { (void)e; g.should_exit = 1; }

/* --- clock visibility toggle ----------------------------------------- */
static void apply_clock_visibility(void)
{
    if (!g.lbl_clock) return;
    if (g.settings && g.settings->show_clock) lv_obj_remove_flag(g.lbl_clock, LV_OBJ_FLAG_HIDDEN);
    else                                      lv_obj_add_flag   (g.lbl_clock, LV_OBJ_FLAG_HIDDEN);
}

/* --- Settings callbacks ---------------------------------------------- */
static void write_backlight(int32_t v)
{
    if (v < 10)  v = 10;
    if (v > 100) v = 100;
    /* Direct sysfs write so the screen reacts on every drag event; the
     * previous IPC-to-Simulink path added a full Simulink tick of latency. */
    FILE *f = fopen(BACKLIGHT_SYSFS, "w");
    if (f) { fprintf(f, "%d\n", (int)v); fclose(f); }
    /* Also keep Simulink's DisplayBrightness parameter in sync so its
     * internal model sees the current level. */
    pid_t pid = ipc_target();
    if (pid >= 0) {
        uint8_t byte = (uint8_t)v;
        if (ipc_write(pid, g.cfg.display_brightness.address, &byte, 1) != 1)
            ipc_invalidate();
    }
}

static void slider_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    write_backlight(lv_slider_get_value(slider));
}

static void clock_toggle_cb(lv_event_t *e)
{
    lv_obj_t *cb = (lv_obj_t *)lv_event_get_target(e);
    g.settings->show_clock = lv_obj_has_state(cb, LV_STATE_CHECKED);
    apply_clock_visibility();
    hmi_settings_save(g.settings);
}

/* --- Tick (5 Hz): clock + per-screen refresh ------------------------ */
static void refresh_live_screen(int push_chart)
{
    uint16_t k15_mv = 0, k30_mv = 0;
    int ok_k15 = read_u16(&g.cfg.k15, &k15_mv) == 0;
    int ok_k30 = read_u16(&g.cfg.k30, &k30_mv) == 0;
    float temp_c = sysinfo_cpu_temp_c();

    if (ok_k15) {
        lv_arc_set_value(g.arc_k15, (int32_t)k15_mv);
        lv_label_set_text_fmt(g.lbl_k15, "%.2f V", k15_mv / 1000.0f);
    } else {
        lv_arc_set_value(g.arc_k15, 0);
        lv_label_set_text(g.lbl_k15, "-");
    }

    if (ok_k30) {
        lv_arc_set_value(g.arc_k30, (int32_t)k30_mv);
        lv_label_set_text_fmt(g.lbl_k30, "%.2f V", k30_mv / 1000.0f);
    } else {
        lv_arc_set_value(g.arc_k30, 0);
        lv_label_set_text(g.lbl_k30, "-");
    }

    if (temp_c > -100.0f) {
        /* Push to chart at 1 Hz only — otherwise the 60-point window
         * (60 s at 1 Hz) shrinks to ~12 s at the fast tick rate. */
        if (push_chart)
            lv_chart_set_next_value(g.chart_temp, g.ser_temp, (int32_t)(temp_c + 0.5f));
        lv_label_set_text_fmt(g.lbl_cpu_temp, "%.1f \xC2\xB0""C", temp_c);
    } else {
        lv_label_set_text(g.lbl_cpu_temp, "-");
    }

    shm_values_t s;
    shm_data_read(&s);

    if (shm_data_valid(s.temperature))
        lv_label_set_text_fmt(g.lbl_temperature, "%.1f", s.temperature);
    else
        lv_label_set_text(g.lbl_temperature, "-");

    if (shm_data_valid(s.pressure)) {
        /* Bar is 0 - 100 bar integer (label keeps 1-decimal precision). */
        int32_t b = (int32_t)(s.pressure + 0.5f);
        if (b < 0)   b = 0;
        if (b > 100) b = 100;
        lv_bar_set_value(g.bar_pressure, b, LV_ANIM_OFF);
        lv_label_set_text_fmt(g.lbl_pressure, "%.1f", s.pressure);
    } else {
        lv_bar_set_value(g.bar_pressure, 0, LV_ANIM_OFF);
        lv_label_set_text(g.lbl_pressure, "-");
    }

    if (shm_data_valid(s.soc)) {
        int32_t pct = (int32_t)(s.soc + 0.5f);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(g.bar_soc, pct, LV_ANIM_OFF);
        lv_label_set_text_fmt(g.lbl_soc, "%.1f", s.soc);
    } else {
        lv_bar_set_value(g.bar_soc, 0, LV_ANIM_OFF);
        lv_label_set_text(g.lbl_soc, "-");
    }

    if (shm_data_valid(s.power))
        lv_label_set_text_fmt(g.lbl_power, "%.1f", s.power);
    else
        lv_label_set_text(g.lbl_power, "-");
}

static void refresh_info_screen(void)
{
    char ip[16];  sysinfo_ipv4(ip, sizeof(ip));
    char mac[18]; sysinfo_mac (mac, sizeof(mac));
    lv_label_set_text(g.info_val_ip,  ip[0]  ? ip  : "-");
    lv_label_set_text(g.info_val_mac, mac[0] ? mac : "-");
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;

    /* Count ticks to throttle things that only need to run once a second —
     * clock label and chart push. (TICK_PERIOD_MS * SLOW_EVERY == 1000.) */
    static int tick_count = 0;
    const int SLOW_EVERY  = 1000 / TICK_PERIOD_MS;  /* 5 fast ticks per slow */
    int do_slow = (tick_count++ % SLOW_EVERY) == 0;

    if (do_slow && g.lbl_clock) {
        time_t now = time(NULL);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M", &tm_local);
        lv_label_set_text(g.lbl_clock, buf);
    }

    lv_obj_t *active = lv_screen_active();
    if      (active == g.scr_live) refresh_live_screen(do_slow);
    else if (active == g.scr_info && do_slow) refresh_info_screen();
}

/* --- Main-screen tile builder ---------------------------------------- */
static lv_obj_t *make_tile(lv_obj_t *parent, const char *title, const char *sub,
                           lv_event_cb_t on_click)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 380, 360);
    lv_obj_set_style_bg_color(btn, BRAND_PANEL, 0);
    lv_obj_set_style_bg_color(btn, BRAND_ORANGE_SOFT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, BRAND_DIVIDER, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 24, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 8, 0);

    lv_obj_t *stripe = lv_obj_create(btn);
    lv_obj_remove_style_all(stripe);
    lv_obj_set_size(stripe, 96, 6);
    lv_obj_set_style_bg_color(stripe, BRAND_ORANGE, 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(stripe, 3, 0);
    lv_obj_set_style_margin_bottom(stripe, 16, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, title);
    style_title(lbl);

    lv_obj_t *lbl_sub = lv_label_create(btn);
    lv_label_set_text(lbl_sub, sub);
    style_sub(lbl_sub);
    lv_obj_set_style_text_align(lbl_sub, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* --- Page header (back button + title) ------------------------------ */
static void build_page_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 72);
    lv_obj_set_style_bg_color(bar, BRAND_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, BRAND_DIVIDER, 0);
    lv_obj_set_style_pad_hor(bar, 24, 0);
    lv_obj_set_style_pad_ver(bar, 12, 0);

    lv_obj_t *back = lv_button_create(bar);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_height(back, 48);
    lv_obj_set_style_pad_hor(back, 20, 0);
    lv_obj_set_style_bg_color(back, BRAND_ORANGE, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, go_main_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
    lv_obj_set_style_text_color(back_lbl, BRAND_PANEL, 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(back_lbl);

    lv_obj_t *ttl = lv_label_create(bar);
    lv_label_set_text(ttl, title);
    style_title(ttl);
    lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 0);
}

/* --- Key/value row used across screens ------------------------------ */
static lv_obj_t *add_kv_row(lv_obj_t *parent, const char *key, const char *val,
                            int key_w)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, key);
    lv_obj_set_width(k, key_w);
    style_sub(k);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, (val && val[0]) ? val : "-");
    lv_obj_set_flex_grow(v, 1);
    lv_obj_set_style_text_color(v, BRAND_DARK, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
    return v;
}

/* The device tree still calls this board "Moduline Display" — the kernel's
 * model/hardware strings predate the HMI1 name. Rewrite it for presentation
 * only: identify_t keeps the verbatim string, so anything matching on what the
 * hardware actually reports still sees the real value. Returns raw unchanged
 * when there is nothing to rewrite or the result would not fit. */
static const char *product_name_for_display(const char *raw, char *buf, size_t buflen)
{
    static const char legacy[] = "Moduline Display";
    static const char current[] = "Moduline HMI1";

    const char *hit = raw ? strstr(raw, legacy) : NULL;
    if (!hit)
        return raw;

    size_t head = (size_t)(hit - raw);
    if (head >= buflen)
        return raw;

    memcpy(buf, raw, head);
    int n = snprintf(buf + head, buflen - head, "%s%s",
                     current, hit + sizeof(legacy) - 1);
    if (n < 0 || (size_t)n >= buflen - head)
        return raw;
    return buf;
}

static lv_obj_t *make_info_panel(lv_obj_t *parent, const char *heading)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(card);
    lv_label_set_text(h, heading);
    style_heading(h);
    return card;
}

/* --- Main screen ----------------------------------------------------- */
static void build_main_screen(void)
{
    g.scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g.scr_main, BRAND_BG, 0);
    lv_obj_set_style_pad_all(g.scr_main, 0, 0);
    lv_obj_remove_flag(g.scr_main, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(g.scr_main);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), 80);
    lv_obj_set_style_bg_color(bar, BRAND_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, BRAND_DIVIDER, 0);
    lv_obj_set_style_pad_hor(bar, 24, 0);

    lv_obj_t *logo = lv_image_create(bar);
    char logo_path[256];
    snprintf(logo_path, sizeof(logo_path), "A:%s/GOcontroll_header.png", g.asset_dir);
    lv_image_set_src(logo, logo_path);
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);

    g.lbl_clock = lv_label_create(bar);
    lv_label_set_text(g.lbl_clock, "--:--");
    lv_obj_set_style_text_font(g.lbl_clock, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(g.lbl_clock, BRAND_DARK, 0);
    lv_obj_align(g.lbl_clock, LV_ALIGN_RIGHT_MID, 0, 0);
    apply_clock_visibility();

    lv_obj_t *row = lv_obj_create(g.scr_main);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 720 - 80);
    lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 32, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    make_tile(row, "Information",
              "Hardware and product\nspecifications", go_info_cb);
    make_tile(row, "Live Data",
              "K15/K30, CPU temp\nand process values", go_live_cb);
    make_tile(row, "Settings",
              "Controller pinning,\nbrightness and clock", go_settings_cb);

    lv_obj_t *footer = lv_label_create(g.scr_main);
    lv_label_set_text(footer, "\xC2\xA9 GOcontroll Moduline HMI1");
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14_ext, 0);
    lv_obj_set_style_text_color(footer, BRAND_SUBTEXT, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/* --- Information screen --------------------------------------------- */
static void build_info_hardware_side(lv_obj_t *side)
{
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 12, 0);
    lv_obj_set_scroll_dir(side, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(side, LV_SCROLLBAR_MODE_AUTO);

    identify_t id;
    identify_collect(&id);

    char ip[16];  sysinfo_ipv4(ip, sizeof(ip));
    char mac[18]; sysinfo_mac (mac, sizeof(mac));

    /* Network card (dynamic — tick_cb refreshes IP/MAC). */
    lv_obj_t *net = make_info_panel(side, "Network");
    g.info_val_ip  = add_kv_row(net, "IP address", ip[0]  ? ip  : "-", 120);
    g.info_val_mac = add_kv_row(net, "MAC",        mac[0] ? mac : "-", 120);

    /* System card. */
    lv_obj_t *sw = make_info_panel(side, "System");
    add_kv_row(sw, "HMI demo",     GOCONTROLL_HMI_DEMO_VERSION, 120);
    add_kv_row(sw, "Distribution", id.os,     120);
    add_kv_row(sw, "Kernel",       id.kernel, 120);
    add_kv_row(sw, "Serial",       id.serial, 120);

    /* Hardware card. */
    lv_obj_t *hw = make_info_panel(side, "Hardware");
    char model_buf[IDENTIFY_STR_MAX], board_buf[IDENTIFY_STR_MAX];
    add_kv_row(hw, "Model",
               product_name_for_display(id.model, model_buf, sizeof model_buf), 120);
    add_kv_row(hw, "Board",
               product_name_for_display(id.hardware, board_buf, sizeof board_buf), 120);

    /* Modules card. */
    lv_obj_t *mods = make_info_panel(side, "Modules");
    if (id.module_count == 0) {
        add_kv_row(mods, "Slots", "no layout file", 120);
    } else {
        int populated = 0;
        for (int i = 0; i < id.module_count; i++) {
            char slot[16]; snprintf(slot, sizeof(slot), "Slot %d", i + 1);
            if (strcmp(id.modules[i].type, "-") == 0) {
                add_kv_row(mods, slot, "empty", 120);
            } else {
                char v[192];
                snprintf(v, sizeof(v), "%s  " LV_SYMBOL_BULLET "  HW %s  " LV_SYMBOL_BULLET "  SW %s",
                         id.modules[i].type,
                         id.modules[i].hw_version,
                         id.modules[i].sw_version);
                add_kv_row(mods, slot, v, 120);
                populated++;
            }
        }
        if (!populated) {
            lv_obj_t *hint = lv_label_create(mods);
            lv_label_set_text(hint, "(no populated slots)");
            style_sub(hint);
        }
    }
}

static void build_info_product_side(lv_obj_t *side)
{
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 12, 0);
    lv_obj_set_scroll_dir(side, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(side, LV_SCROLLBAR_MODE_AUTO);

    /* Hero card with heading, tagline and long description. */
    lv_obj_t *hero = lv_obj_create(side);
    style_panel(hero);
    lv_obj_set_size(hero, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hero, 6, 0);
    lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hero);
    lv_label_set_text(title, "Moduline HMI1");
    style_heading(title);

    lv_obj_t *tag = lv_label_create(hero);
    lv_label_set_text(tag, g_product_tagline);
    lv_obj_set_style_text_color(tag, BRAND_ORANGE, 0);
    lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(tag, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(tag, lv_pct(100));

    lv_obj_t *desc = lv_label_create(hero);
    lv_label_set_text(desc, g_product_description);
    style_sub(desc);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(desc, lv_pct(100));

    /* One card per section. */
    for (size_t i = 0; i < g_product_section_count; i++) {
        const product_info_section_t *sec = &g_product_sections[i];
        lv_obj_t *card = make_info_panel(side, sec->heading);
        for (size_t j = 0; j < sec->row_count; j++)
            add_kv_row(card, sec->rows[j].key, sec->rows[j].value, 120);
    }
}

static void build_info_screen(void)
{
    g.scr_info = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g.scr_info, BRAND_BG, 0);
    lv_obj_set_style_pad_all(g.scr_info, 0, 0);
    lv_obj_remove_flag(g.scr_info, LV_OBJ_FLAG_SCROLLABLE);

    build_page_header(g.scr_info, "Information");

    lv_obj_t *body = lv_obj_create(g.scr_info);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), 720 - 72);
    lv_obj_align(body, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(body, 16, 0);
    lv_obj_set_style_pad_column(body, 16, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = lv_obj_create(body);
    lv_obj_remove_style_all(left);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, lv_pct(100));
    lv_obj_set_style_pad_all(left, 0, 0);

    lv_obj_t *right = lv_obj_create(body);
    lv_obj_remove_style_all(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_style_pad_all(right, 0, 0);

    build_info_hardware_side(left);
    build_info_product_side(right);
}

/* --- Live Data screen ------------------------------------------------ */

static void style_arc_gauge(lv_obj_t *arc, lv_color_t indicator_color)
{
    lv_obj_set_size(arc, 200, 200);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, GAUGE_RANGE_MV);
    lv_arc_set_value(arc, 0);
    /* Non-interactive: no knob drag, no click. */
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, BRAND_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, indicator_color, LV_PART_INDICATOR);
    /* Hide the draggable knob. */
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
}

/* A card wrapping an arc gauge with its heading and value label. */
static void build_gauge_card(lv_obj_t *parent, const char *heading,
                             lv_color_t color,
                             lv_obj_t **out_arc, lv_obj_t **out_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    lv_obj_set_size(card, 260, 260);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(card);
    lv_label_set_text(h, heading);
    style_sub(h);

    lv_obj_t *arc = lv_arc_create(card);
    style_arc_gauge(arc, color);

    /* Overlay the value label in the middle of the arc via absolute align. */
    lv_obj_t *val = lv_label_create(arc);
    lv_label_set_text(val, "-");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(val, BRAND_DARK, 0);
    lv_obj_center(val);

    *out_arc   = arc;
    *out_label = val;
}

/* Temperature chart card (line chart + current-value label above). */
static lv_obj_t *build_temp_chart_card(lv_obj_t *parent,
                                       lv_obj_t **out_label,
                                       lv_chart_series_t **out_series)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, 260);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *h = lv_label_create(header);
    lv_label_set_text(h, "CPU temperature");
    style_sub(h);

    lv_obj_t *val = lv_label_create(header);
    lv_label_set_text(val, "-");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(val, BRAND_DARK, 0);
    *out_label = val;

    lv_obj_t *chart = lv_chart_create(card);
    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 20, 80);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 1, 0);

    *out_series = lv_chart_add_series(chart, BRAND_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
    return chart;
}

/* Shm-value card with heading + big value + unit + optional bar. */
typedef enum {
    SHM_CARD_TEXT,
    SHM_CARD_BAR,
} shm_card_mode_t;

static void build_shm_card(lv_obj_t *parent, const char *heading,
                           const char *unit, shm_card_mode_t mode,
                           int32_t bar_max,
                           lv_obj_t **out_val_label,
                           lv_obj_t **out_bar)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, 160);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(card);
    lv_label_set_text(h, heading);
    style_sub(h);

    /* Value row: number (large) + unit (sub). */
    lv_obj_t *vrow = lv_obj_create(card);
    lv_obj_remove_style_all(vrow);
    lv_obj_set_size(vrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(vrow, 6, 0);

    lv_obj_t *val = lv_label_create(vrow);
    lv_label_set_text(val, "-");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(val, BRAND_DARK, 0);
    *out_val_label = val;

    lv_obj_t *u = lv_label_create(vrow);
    lv_label_set_text(u, unit);
    style_sub(u);

    if (mode == SHM_CARD_BAR) {
        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_size(bar, lv_pct(100), 14);
        lv_bar_set_range(bar, 0, bar_max);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, BRAND_DIVIDER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, BRAND_ORANGE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);
        if (out_bar) *out_bar = bar;
    } else if (out_bar) {
        *out_bar = NULL;
    }
}

static void build_live_screen(void)
{
    g.scr_live = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g.scr_live, BRAND_BG, 0);
    lv_obj_set_style_pad_all(g.scr_live, 0, 0);
    lv_obj_remove_flag(g.scr_live, LV_OBJ_FLAG_SCROLLABLE);

    build_page_header(g.scr_live, "Live Data");

    lv_obj_t *body = lv_obj_create(g.scr_live);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), 720 - 72);
    lv_obj_align(body, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 16, 0);
    lv_obj_set_style_pad_row(body, 16, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* Row 1: K15 arc | K30 arc | CPU temp chart. */
    lv_obj_t *row1 = lv_obj_create(body);
    lv_obj_remove_style_all(row1);
    lv_obj_set_size(row1, lv_pct(100), 260);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row1, 16, 0);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    build_gauge_card(row1, "K15 voltage", BRAND_ORANGE, &g.arc_k15, &g.lbl_k15);
    build_gauge_card(row1, "K30 voltage", BRAND_DARK,   &g.arc_k30, &g.lbl_k30);
    g.chart_temp = build_temp_chart_card(row1, &g.lbl_cpu_temp, &g.ser_temp);

    /* Row 2: four shm cards — Temperature (text), Pressure (bar),
     * SOC (bar), Power (text). */
    lv_obj_t *row2 = lv_obj_create(body);
    lv_obj_remove_style_all(row2);
    lv_obj_set_size(row2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row2, 16, 0);
    lv_obj_remove_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    build_shm_card(row2, "Temperature", "\xC2\xB0""C", SHM_CARD_TEXT, 0,
                   &g.lbl_temperature, NULL);
    build_shm_card(row2, "Pressure", "bar", SHM_CARD_BAR, 100,
                   &g.lbl_pressure, &g.bar_pressure);
    build_shm_card(row2, "SOC", "%", SHM_CARD_BAR, 100,
                   &g.lbl_soc, &g.bar_soc);
    build_shm_card(row2, "Power", "kW", SHM_CARD_TEXT, 0,
                   &g.lbl_power, NULL);
}

/* --- Settings screen (merged with Configuration) -------------------- */
static void build_pinning_panel(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    lv_obj_set_size(card, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(card);
    lv_label_set_text(h, "Controller pinning");
    style_heading(h);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Connector A  " LV_SYMBOL_BULLET "  Power and CAN buses");
    style_sub(sub);

    lv_obj_t *table = lv_table_create(card);
    lv_obj_set_flex_grow(table, 1);
    lv_obj_set_width(table, lv_pct(100));
    lv_obj_set_style_bg_color(table, BRAND_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(table, BRAND_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(table, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(table, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(table, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_text_color(table, BRAND_DARK, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(table, 10, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(table, 6,  LV_PART_ITEMS);
    lv_obj_set_style_border_color(table, BRAND_DIVIDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);

    lv_table_set_column_count(table, 3);
    lv_table_set_column_width(table, 0, 70);
    lv_table_set_column_width(table, 1, 130);
    lv_table_set_column_width(table, 2, 280);
    lv_table_set_row_count(table, (uint16_t)(g_hmi1_system_pin_count + 1));
    lv_table_set_cell_value(table, 0, 0, "Pin");
    lv_table_set_cell_value(table, 0, 1, "Signal");
    lv_table_set_cell_value(table, 0, 2, "Description");
    for (size_t i = 0; i < g_hmi1_system_pin_count; i++) {
        lv_table_set_cell_value(table, (uint16_t)(i + 1), 0, g_hmi1_system_pins[i].pin);
        lv_table_set_cell_value(table, (uint16_t)(i + 1), 1, g_hmi1_system_pins[i].signal);
        lv_table_set_cell_value(table, (uint16_t)(i + 1), 2, g_hmi1_system_pins[i].description);
    }
}

static void build_settings_controls(lv_obj_t *parent)
{
    /* Image card with the HMI1 back view. */
    lv_obj_t *img_card = lv_obj_create(parent);
    style_panel(img_card);
    lv_obj_set_size(img_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(img_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(img_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(img_card, 6, 0);
    lv_obj_remove_flag(img_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_image_create(img_card);
    char p[256];
    snprintf(p, sizeof(p), "A:%s/hmi1_back.png", g.asset_dir);
    lv_image_set_src(img, p);

    lv_obj_t *caption = lv_label_create(img_card);
    lv_label_set_text(caption, "Moduline HMI1  " LV_SYMBOL_BULLET "  rear view");
    style_sub(caption);

    /* Brightness card. */
    lv_obj_t *bcard = lv_obj_create(parent);
    style_panel(bcard);
    lv_obj_set_size(bcard, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bcard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bcard, 8, 0);
    lv_obj_remove_flag(bcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bh = lv_label_create(bcard);
    lv_label_set_text(bh, "Display brightness");
    style_heading(bh);

    g.slider_bright = lv_slider_create(bcard);
    lv_slider_set_range(g.slider_bright, 10, 100);
    lv_slider_set_value(g.slider_bright, read_backlight(), LV_ANIM_OFF);
    lv_obj_set_width(g.slider_bright, lv_pct(100));
    lv_obj_set_height(g.slider_bright, 18);
    lv_obj_set_style_bg_color(g.slider_bright, BRAND_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.slider_bright, BRAND_ORANGE,  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g.slider_bright, BRAND_ORANGE,  LV_PART_KNOB);
    lv_obj_add_event_cb(g.slider_bright, slider_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Clock toggle + Exit — combined card to save space. */
    lv_obj_t *bottom = lv_obj_create(parent);
    style_panel(bottom);
    lv_obj_set_size(bottom, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bottom, 12, 0);
    lv_obj_remove_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);

    g.cb_clock = lv_checkbox_create(bottom);
    lv_checkbox_set_text(g.cb_clock, "Show clock");
    lv_obj_set_style_text_font(g.cb_clock, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g.cb_clock, BRAND_DARK, 0);
    lv_obj_set_style_bg_color(g.cb_clock, BRAND_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(g.cb_clock, BRAND_SUBTEXT, LV_PART_INDICATOR);
    if (g.settings && g.settings->show_clock) lv_obj_add_state(g.cb_clock, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g.cb_clock, clock_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *exit_btn = lv_button_create(bottom);
    lv_obj_set_size(exit_btn, 140, 44);
    lv_obj_set_style_bg_color(exit_btn, BRAND_DARK, 0);
    lv_obj_set_style_bg_opa(exit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(exit_btn, 8, 0);
    lv_obj_set_style_shadow_width(exit_btn, 0, 0);
    lv_obj_add_event_cb(exit_btn, exit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = lv_label_create(exit_btn);
    lv_label_set_text(el, "Exit demo");
    lv_obj_set_style_text_color(el, BRAND_PANEL, 0);
    lv_obj_set_style_text_font(el, &lv_font_montserrat_14, 0);
    lv_obj_center(el);
}

static void build_settings_screen(void)
{
    g.scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g.scr_settings, BRAND_BG, 0);
    lv_obj_set_style_pad_all(g.scr_settings, 0, 0);
    lv_obj_remove_flag(g.scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    build_page_header(g.scr_settings, "Settings");

    lv_obj_t *body = lv_obj_create(g.scr_settings);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), 720 - 72);
    lv_obj_align(body, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(body, 16, 0);
    lv_obj_set_style_pad_column(body, 16, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* Left column — pinning (wider). */
    lv_obj_t *left = lv_obj_create(body);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, 520, lv_pct(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    build_pinning_panel(left);

    /* Right column — image + controls. */
    lv_obj_t *right = lv_obj_create(body);
    lv_obj_remove_style_all(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_row(right, 12, 0);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    build_settings_controls(right);
}

/* --- Splash fade-in --------------------------------------------------
 *
 * The boot-time gocontroll-hmi-splash service holds a black-framed logo on
 * the display until we take DRM master. To avoid a hard cut from that
 * final black frame to the live dashboard, drop a black overlay on top of
 * the main screen and fade its opacity from 255 to 0 — the dashboard is
 * revealed through the fade rather than snapping into view. */

static void set_opa_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void remove_overlay_anim_cb(lv_anim_t *a)
{
    lv_obj_delete((lv_obj_t *)a->var);
}

static void start_fade_in_from_black(void)
{
    lv_obj_t *overlay = lv_obj_create(g.scr_main);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    /* Cover any touches during the fade so a tap on the about-to-appear
     * dashboard doesn't fire through half-way into the transition. */
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(overlay);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, overlay);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 450);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, set_opa_anim_cb);
    lv_anim_set_completed_cb(&a, remove_overlay_anim_cb);
    lv_anim_start(&a);
}

/* --- Public entry points -------------------------------------------- */
void ui_build(pid_t demo_pid, const hmi_config_t *cfg, const char *asset_dir,
              hmi_settings_t *settings)
{
    memset(&g, 0, sizeof(g));
    g.demo_pid = demo_pid;
    g.cfg      = *cfg;
    g.settings = settings;
    snprintf(g.asset_dir, sizeof(g.asset_dir), "%s", asset_dir);

    /* Ensure /dev/shm process fields have something readable on first boot
     * so the Live screen does not show dashes until the producer wakes up. */
    shm_data_seed_defaults();

    build_main_screen();
    build_info_screen();
    build_live_screen();
    build_settings_screen();

    lv_screen_load(g.scr_main);
    start_fade_in_from_black();

    g.tick_timer = lv_timer_create(tick_cb, TICK_PERIOD_MS, NULL);
    lv_timer_ready(g.tick_timer);
}

void ui_shutdown(void)
{
    if (g.tick_timer) {
        lv_timer_delete(g.tick_timer);
        g.tick_timer = NULL;
    }
}

int ui_should_exit(void) { return g.should_exit; }
