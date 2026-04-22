#define _GNU_SOURCE
#include "ui.h"
#include "identify.h"
#include "ipc.h"
#include "settings.h"
#include "sysinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

/* Montserrat 14 regenerated locally to include U+00A9 (©) for the main-screen
 * footer — the stock LVGL build only covers ASCII + U+00B0 + U+2022. The .c
 * source lives in src/fonts/ so we don't patch the lvgl submodule. */
LV_FONT_DECLARE(lv_font_montserrat_14_ext);

/* --- Brand palette (sampled from assets/GOcontroll.png) ---------------- */
#define BRAND_ORANGE   lv_color_hex(0xE94E24)
#define BRAND_DARK     lv_color_hex(0x1C1C1C)
#define BRAND_BG       lv_color_hex(0xF2F2F2)
#define BRAND_PANEL    lv_color_hex(0xFFFFFF)
#define BRAND_SUBTEXT  lv_color_hex(0x6B6B6B)
#define BRAND_DIVIDER  lv_color_hex(0xD9D9D9)
#define BRAND_ORANGE_SOFT lv_color_hex(0xFBE2D9)

#define CHART_POINTS   60   /* 60s rolling window at 1 Hz */

static struct {
    pid_t            demo_pid;
    hmi_config_t     cfg;
    hmi_settings_t  *settings;
    char             asset_dir[128];
    int              should_exit;

    /* Screens */
    lv_obj_t *scr_main, *scr_info, *scr_settings;

    /* Main screen */
    lv_obj_t    *lbl_clock;
    lv_timer_t  *tick_timer;   /* 1 Hz — clock, sysinfo, chart push */

    /* Information screen */
    lv_obj_t          *val_ip, *val_temp, *val_k15, *val_k30;
    lv_obj_t          *chart_volts, *chart_temp;
    lv_chart_series_t *ser_k15, *ser_k30, *ser_temp;

    /* Settings screen */
    lv_obj_t *slider_bright, *cb_clock;
} g;

/* --- asap scalar read helper (same as original) ----------------------- */
static int read_u16(const asap_element_t *el, uint16_t *out)
{
    if (g.demo_pid < 0) return -1;
    size_t n = asap_datatype_size(el->type);
    uint8_t buf[8] = {0};
    if (ipc_read(g.demo_pid, el->address, buf, n) != (ssize_t)n) return -1;
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

/* --- small styling helpers -------------------------------------------- */
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

/* --- navigation callbacks --------------------------------------------- */
static void go_main_cb    (lv_event_t *e) { (void)e; lv_screen_load(g.scr_main); }
static void go_info_cb    (lv_event_t *e) { (void)e; lv_screen_load(g.scr_info); }
static void go_settings_cb(lv_event_t *e) { (void)e; lv_screen_load(g.scr_settings); }

static void exit_cb(lv_event_t *e) { (void)e; g.should_exit = 1; }

/* --- Main screen: clock visibility ------------------------------------ */
static void apply_clock_visibility(void)
{
    if (!g.lbl_clock) return;
    if (g.settings && g.settings->show_clock) lv_obj_remove_flag(g.lbl_clock, LV_OBJ_FLAG_HIDDEN);
    else                                      lv_obj_add_flag   (g.lbl_clock, LV_OBJ_FLAG_HIDDEN);
}

/* --- Settings callbacks ----------------------------------------------- */
static void slider_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    if (g.demo_pid >= 0 && v >= 0 && v <= 255) {
        uint8_t byte = (uint8_t)v;
        ipc_write(g.demo_pid, g.cfg.display_brightness.address, &byte, 1);
    }
}

static void clock_toggle_cb(lv_event_t *e)
{
    lv_obj_t *cb = (lv_obj_t *)lv_event_get_target(e);
    g.settings->show_clock = lv_obj_has_state(cb, LV_STATE_CHECKED);
    apply_clock_visibility();
    hmi_settings_save(g.settings);
}

/* --- Tick: clock label + (if info screen loaded) stats/charts --------- */
static void tick_cb(lv_timer_t *t)
{
    (void)t;

    /* Clock — every second update the label text. The widget itself is
     * hidden when show_clock is false so the draw path skips it. */
    if (g.lbl_clock) {
        time_t now = time(NULL);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M", &tm_local);
        lv_label_set_text(g.lbl_clock, buf);
    }

    /* Info screen: stat cards + chart push. Skip when the info screen
     * isn't the active one — cheaper, and the chart is paused anyway. */
    if (lv_screen_active() != g.scr_info) return;

    /* Sensors. */
    uint16_t k15_mv = 0, k30_mv = 0;
    int ok_k15 = read_u16(&g.cfg.k15, &k15_mv) == 0;
    int ok_k30 = read_u16(&g.cfg.k30, &k30_mv) == 0;
    float temp_c = sysinfo_cpu_temp_c();
    char ip[16]; sysinfo_ipv4(ip, sizeof(ip));

    if (ok_k15) lv_label_set_text_fmt(g.val_k15, "%.2f V", k15_mv / 1000.0f);
    else        lv_label_set_text    (g.val_k15, "-");

    if (ok_k30) lv_label_set_text_fmt(g.val_k30, "%.2f V", k30_mv / 1000.0f);
    else        lv_label_set_text    (g.val_k30, "-");

    if (temp_c > -100.0f) lv_label_set_text_fmt(g.val_temp, "%.1f °C", temp_c);
    else                  lv_label_set_text    (g.val_temp, "-");

    lv_label_set_text(g.val_ip, ip[0] ? ip : "-");

    /* Chart push. Values are mV and °C directly — cheap to plot. */
    if (ok_k15) lv_chart_set_next_value(g.chart_volts, g.ser_k15, (int32_t)k15_mv);
    if (ok_k30) lv_chart_set_next_value(g.chart_volts, g.ser_k30, (int32_t)k30_mv);
    if (temp_c > -100.0f)
        lv_chart_set_next_value(g.chart_temp, g.ser_temp, (int32_t)(temp_c + 0.5f));
}

/* --- Tile builder used on the main screen ----------------------------- */
static lv_obj_t *make_tile(lv_obj_t *parent, const char *title, const char *sub,
                           lv_event_cb_t on_click)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 420, 360);
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

    /* Orange accent stripe at the top of each tile. */
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

/* --- Top bar used on Info & Settings: back button + page title -------- */
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

/* --- Chart with y-axis scale ------------------------------------------
 * LVGL 9 dropped the chart axis-tick API, so we build a tiny flex row:
 *   [ y-scale column | chart ]
 * with max/mid/min labels distributed SPACE_BETWEEN along the column so they
 * line up with the chart's plot top/centre/bottom. Internal chart values stay
 * in whatever unit the caller is already pushing (mV, raw °C, …) — the label
 * strings are just text and can express a different unit if desired. */
static lv_obj_t *make_chart_with_yscale(lv_obj_t *parent, int height,
                                        int32_t y_min, int32_t y_max,
                                        const char *lbl_min,
                                        const char *lbl_mid,
                                        const char *lbl_max)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), height);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scale = lv_obj_create(row);
    lv_obj_remove_style_all(scale);
    lv_obj_set_size(scale, 56, lv_pct(100));
    lv_obj_set_flex_flow(scale, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scale, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                 LV_FLEX_ALIGN_END,
                                 LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(scale, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l_max = lv_label_create(scale);
    lv_label_set_text(l_max, lbl_max); style_sub(l_max);
    lv_obj_t *l_mid = lv_label_create(scale);
    lv_label_set_text(l_mid, lbl_mid); style_sub(l_mid);
    lv_obj_t *l_min = lv_label_create(scale);
    lv_label_set_text(l_min, lbl_min); style_sub(l_min);

    lv_obj_t *chart = lv_chart_create(row);
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_height(chart, lv_pct(100));
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 1, 0);  /* one mid-line matching lbl_mid */
    return chart;
}

/* --- Stat card (one of four on the Info screen) ----------------------- */
static lv_obj_t *make_stat_card(lv_obj_t *parent, const char *label, int width)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    lv_obj_set_size(card, width, 96);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 4, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, label);
    style_sub(lbl);

    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, "-");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(val, BRAND_DARK, 0);
    return val; /* caller stores this to update the value later. */
}

/* --- Screen builders -------------------------------------------------- */
static void build_main_screen(void)
{
    g.scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g.scr_main, BRAND_BG, 0);
    lv_obj_set_style_pad_all(g.scr_main, 0, 0);
    lv_obj_remove_flag(g.scr_main, LV_OBJ_FLAG_SCROLLABLE);

    /* Top bar: logo left, clock right. */
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

    /* Tile row — centered vertically below the bar. */
    lv_obj_t *row = lv_obj_create(g.scr_main);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 720 - 80);
    lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 40, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    make_tile(row, "Information",
              "Live sensor data,\nsystem status and graphs", go_info_cb);
    make_tile(row, "Settings",
              "Backlight, preferences\nand demo info", go_settings_cb);

    /* Footer with the real © glyph — possible because we use the extended
     * Montserrat-14 that includes U+00A9. */
    lv_obj_t *footer = lv_label_create(g.scr_main);
    lv_label_set_text(footer, "\xC2\xA9 GOcontroll Moduline HMI1");
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14_ext, 0);
    lv_obj_set_style_text_color(footer, BRAND_SUBTEXT, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/* Info-screen "System" tab helpers ------------------------------------ */

static void add_kv_row(lv_obj_t *parent, const char *key, const char *val)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, key);
    lv_obj_set_width(k, 120);
    style_sub(k);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, (val && val[0]) ? val : "-");
    lv_obj_set_flex_grow(v, 1);
    lv_obj_set_style_text_color(v, BRAND_DARK, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
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

static void build_live_tab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_row(tab, 12, 0);
    lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    /* Stat row — 4 cards. */
    lv_obj_t *stats = lv_obj_create(tab);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, lv_pct(100), 96);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const int card_w = 290;
    g.val_ip   = make_stat_card(stats, "IP address",     card_w);
    g.val_temp = make_stat_card(stats, "CPU temperature", card_w);
    g.val_k15  = make_stat_card(stats, "K15",             card_w);
    g.val_k30  = make_stat_card(stats, "K30",             card_w);

    /* Two charts stacked, splitting the remaining height equally. */
    const int chart_h = (720 - 72 - 48 - 2*12 - 96 - 2*12) / 2;

    lv_obj_t *card1 = lv_obj_create(tab);
    style_panel(card1);
    lv_obj_set_size(card1, lv_pct(100), chart_h);
    lv_obj_set_flex_flow(card1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card1, 6, 0);
    lv_obj_remove_flag(card1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t1 = lv_label_create(card1);
    lv_label_set_text(t1, "K15 / K30 (V)  " LV_SYMBOL_BULLET "  last 60 s");
    style_heading(t1);

    /* Internal values stay in mV for full resolution; the y-axis labels
     * translate to volts so the displayed scale matches the stat cards. */
    g.chart_volts = make_chart_with_yscale(card1, chart_h - 56, 0, 16000,
                                           "0 V", "8 V", "16 V");
    g.ser_k15 = lv_chart_add_series(g.chart_volts, BRAND_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
    g.ser_k30 = lv_chart_add_series(g.chart_volts, BRAND_DARK,   LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *card2 = lv_obj_create(tab);
    style_panel(card2);
    lv_obj_set_size(card2, lv_pct(100), chart_h);
    lv_obj_set_flex_flow(card2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card2, 6, 0);
    lv_obj_remove_flag(card2, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t2 = lv_label_create(card2);
    lv_label_set_text(t2, "CPU temperature (°C)  " LV_SYMBOL_BULLET "  last 60 s");
    style_heading(t2);

    g.chart_temp = make_chart_with_yscale(card2, chart_h - 56, 20, 80,
                                          "20 °C", "50 °C", "80 °C");
    g.ser_temp = lv_chart_add_series(g.chart_temp, BRAND_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
}

static void build_system_tab(lv_obj_t *tab)
{
    identify_t id;
    identify_collect(&id);

    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_row(tab, 12, 0);
    /* Keep the tab scrollable in case a controller has many modules. */

    lv_obj_t *sw = make_info_panel(tab, "Software");
    add_kv_row(sw, "OS",     id.os);
    add_kv_row(sw, "Kernel", id.kernel);

    lv_obj_t *hw = make_info_panel(tab, "Hardware");
    add_kv_row(hw, "Model",  id.model);
    add_kv_row(hw, "Board",  id.hardware);
    add_kv_row(hw, "Serial", id.serial);

    lv_obj_t *mods = make_info_panel(tab, "Modules");
    if (id.module_count == 0) {
        add_kv_row(mods, "Slots",
                   "no module layout file, run `go-modules scan`");
    } else {
        int real = 0;
        for (int i = 0; i < id.module_count; i++) {
            char slot[16]; snprintf(slot, sizeof(slot), "Slot %d", i + 1);
            if (strcmp(id.modules[i].type, "-") == 0) {
                add_kv_row(mods, slot, "empty");
            } else {
                char v[192];
                snprintf(v, sizeof(v), "%s  " LV_SYMBOL_BULLET "  HW %s  " LV_SYMBOL_BULLET "  SW %s",
                         id.modules[i].type,
                         id.modules[i].hw_version,
                         id.modules[i].sw_version);
                add_kv_row(mods, slot, v);
                real++;
            }
        }
        if (!real) {
            lv_obj_t *hint = lv_label_create(mods);
            lv_label_set_text(hint, "(no populated slots on this controller)");
            style_sub(hint);
        }
    }
}

static void build_info_screen(void)
{
    g.scr_info = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g.scr_info, BRAND_BG, 0);
    lv_obj_set_style_pad_all(g.scr_info, 0, 0);
    lv_obj_remove_flag(g.scr_info, LV_OBJ_FLAG_SCROLLABLE);

    build_page_header(g.scr_info, "Information");

    /* Tabview fills the body region. Brand-style the tab buttons so the
     * selected tab matches the orange accent used everywhere else. */
    lv_obj_t *tv = lv_tabview_create(g.scr_info);
    lv_obj_set_size(tv, lv_pct(100), 720 - 72);
    lv_obj_align(tv, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_tabview_set_tab_bar_size(tv, 48);
    lv_obj_set_style_bg_color(tv, BRAND_BG, 0);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(tab_bar, BRAND_PANEL, 0);
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(tab_bar, 1, 0);
    lv_obj_set_style_border_color(tab_bar, BRAND_DIVIDER, 0);
    lv_obj_set_style_text_color(tab_bar, BRAND_SUBTEXT, 0);
    lv_obj_set_style_text_color(tab_bar, BRAND_ORANGE, LV_STATE_CHECKED);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_24, 0);

    lv_obj_t *tab_live = lv_tabview_add_tab(tv, "Live");
    lv_obj_t *tab_sys  = lv_tabview_add_tab(tv, "System");
    build_live_tab(tab_live);
    build_system_tab(tab_sys);
}

static void build_settings_screen(void)
{
    g.scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g.scr_settings, BRAND_BG, 0);
    lv_obj_set_style_pad_all(g.scr_settings, 0, 0);
    lv_obj_remove_flag(g.scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    build_page_header(g.scr_settings, "Settings");

    /* Body */
    lv_obj_t *body = lv_obj_create(g.scr_settings);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), 720 - 72);
    lv_obj_align(body, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(body, 64, 0);
    lv_obj_set_style_pad_ver(body, 24, 0);
    lv_obj_set_style_pad_row(body, 16, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* Brightness card */
    lv_obj_t *bcard = lv_obj_create(body);
    style_panel(bcard);
    lv_obj_set_size(bcard, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bcard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bcard, 10, 0);
    lv_obj_remove_flag(bcard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bh = lv_label_create(bcard);
    lv_label_set_text(bh, "Display brightness");
    style_heading(bh);

    g.slider_bright = lv_slider_create(bcard);
    lv_slider_set_range(g.slider_bright, 10, 100);
    lv_slider_set_value(g.slider_bright, read_backlight(), LV_ANIM_OFF);
    lv_obj_set_width(g.slider_bright, lv_pct(100));
    lv_obj_set_height(g.slider_bright, 20);
    lv_obj_set_style_bg_color(g.slider_bright, BRAND_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.slider_bright, BRAND_ORANGE,  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g.slider_bright, BRAND_ORANGE,  LV_PART_KNOB);
    lv_obj_add_event_cb(g.slider_bright, slider_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Clock toggle card */
    lv_obj_t *cbcard = lv_obj_create(body);
    style_panel(cbcard);
    lv_obj_set_size(cbcard, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cbcard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cbcard, 6, 0);
    lv_obj_remove_flag(cbcard, LV_OBJ_FLAG_SCROLLABLE);

    g.cb_clock = lv_checkbox_create(cbcard);
    lv_checkbox_set_text(g.cb_clock, "Show clock on main screen");
    lv_obj_set_style_text_font(g.cb_clock, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g.cb_clock, BRAND_DARK, 0);
    lv_obj_set_style_bg_color(g.cb_clock, BRAND_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(g.cb_clock, BRAND_SUBTEXT, LV_PART_INDICATOR);
    if (g.settings && g.settings->show_clock) lv_obj_add_state(g.cb_clock, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g.cb_clock, clock_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* About card */
    lv_obj_t *about = lv_obj_create(body);
    style_panel(about);
    lv_obj_set_size(about, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(about, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(about, 6, 0);
    lv_obj_remove_flag(about, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ah = lv_label_create(about);
    lv_label_set_text(ah, "About this demo");
    style_heading(ah);

    lv_obj_t *abody = lv_label_create(about);
    lv_label_set_text(abody,
        "Framework: LVGL 9.5 on direct KMS/DRM, no compositor.\n"
        "Renderer: software rasterizer with ARM NEON on Cortex-A53.\n"
        "Display: 1280x720 XRGB8888, 180° rotation via cached shadow buffer.\n"
        "Repo: https://github.com/GOcontroll/GOcontroll-HMI-Demo\n"
        "Default users: root:root (no ssh) / GOcontroll:Moduline");
    style_sub(abody);
    lv_obj_set_style_text_font(abody, &lv_font_montserrat_14, 0);

    /* Exit button. */
    lv_obj_t *exit_btn = lv_button_create(body);
    lv_obj_set_size(exit_btn, 220, 56);
    lv_obj_set_style_bg_color(exit_btn, BRAND_DARK, 0);
    lv_obj_set_style_bg_opa(exit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(exit_btn, 8, 0);
    lv_obj_set_style_shadow_width(exit_btn, 0, 0);
    lv_obj_set_style_align(exit_btn, LV_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(exit_btn, exit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = lv_label_create(exit_btn);
    lv_label_set_text(el, "Exit demo");
    lv_obj_set_style_text_color(el, BRAND_PANEL, 0);
    lv_obj_set_style_text_font(el, &lv_font_montserrat_24, 0);
    lv_obj_center(el);
}

/* --- Public entry points ---------------------------------------------- */
void ui_build(pid_t demo_pid, const hmi_config_t *cfg, const char *asset_dir,
              hmi_settings_t *settings)
{
    memset(&g, 0, sizeof(g));
    g.demo_pid = demo_pid;
    g.cfg      = *cfg;
    g.settings = settings;
    snprintf(g.asset_dir, sizeof(g.asset_dir), "%s", asset_dir);

    build_main_screen();
    build_info_screen();
    build_settings_screen();

    lv_screen_load(g.scr_main);

    /* 1 Hz tick drives clock + info screen sampling. */
    g.tick_timer = lv_timer_create(tick_cb, 1000, NULL);
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
