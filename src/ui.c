#define _GNU_SOURCE
#include "ui.h"
#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

static struct {
    pid_t           demo_pid;
    hmi_config_t    cfg;
    lv_obj_t       *lbl_k15;
    lv_obj_t       *lbl_k30;
    lv_obj_t       *slider;
    lv_timer_t     *voltage_timer;
    uint16_t        last_k15;
    uint16_t        last_k30;
    int             have_last;
    int             should_exit;
} g;

/* Read a scalar asap_element into a uint16. Works for uint8/uint16 signed or
 * unsigned; demo only ever reads those. */
static int read_u16(const asap_element_t *el, uint16_t *out)
{
    if (g.demo_pid < 0) return -1;
    size_t n = asap_datatype_size(el->type);
    uint8_t buf[8] = {0};
    ssize_t got = ipc_read(g.demo_pid, el->address, buf, n);
    if (got != (ssize_t)n) return -1;
    switch (el->type) {
    case DTYPE_UINT8:
    case DTYPE_INT8:     *out = buf[0];                     return 0;
    case DTYPE_UINT16:
    case DTYPE_INT16:    *out = (uint16_t)(buf[0] | (buf[1] << 8)); return 0;
    default:             *out = 0;                          return -1;
    }
}

static void voltage_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint16_t k15 = 0, k30 = 0;
    int ok_k15 = read_u16(&g.cfg.k15, &k15) == 0;
    int ok_k30 = read_u16(&g.cfg.k30, &k30) == 0;

    /* Skip relabel if value unchanged — saves redraw cost on this CPU-rendered UI. */
    if (!g.have_last || !ok_k15 || k15 != g.last_k15) {
        if (ok_k15) lv_label_set_text_fmt(g.lbl_k15, "K15: %umV", (unsigned)k15);
        else        lv_label_set_text(g.lbl_k15, "K15: -mV");
        g.last_k15 = k15;
    }
    if (!g.have_last || !ok_k30 || k30 != g.last_k30) {
        if (ok_k30) lv_label_set_text_fmt(g.lbl_k30, "K30: %umV", (unsigned)k30);
        else        lv_label_set_text(g.lbl_k30, "K30: -mV");
        g.last_k30 = k30;
    }
    g.have_last = 1;
}

static void slider_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    if (g.demo_pid >= 0 && v >= 0 && v <= 255) {
        uint8_t byte = (uint8_t)v;
        ipc_write(g.demo_pid, g.cfg.display_brightness.address, &byte, 1);
    }
}

static void exit_clicked_cb(lv_event_t *e)
{
    (void)e;
    g.should_exit = 1;
}

static int32_t read_current_brightness(void)
{
    FILE *f = fopen("/sys/class/backlight/max25014/brightness", "r");
    if (!f) return 50;
    int v = 50;
    if (fscanf(f, "%d", &v) != 1) v = 50;
    fclose(f);
    if (v < 10)  v = 10;
    if (v > 100) v = 100;
    return v;
}

void ui_build(pid_t demo_pid, const hmi_config_t *cfg, const char *asset_dir)
{
    memset(&g, 0, sizeof(g));
    g.demo_pid = demo_pid;
    g.cfg      = *cfg;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF5F5F5), 0);

    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 20, 0);
    lv_obj_set_style_pad_hor(col, 200, 0);

    /* Logo */
    lv_obj_t *img = lv_image_create(col);
    char logo_path[300];
    snprintf(logo_path, sizeof(logo_path), "A:%s/GOcontroll.png", asset_dir);
    lv_image_set_src(img, logo_path);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);

    /* K30 / K15 row */
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g.lbl_k30 = lv_label_create(row);
    lv_label_set_text(g.lbl_k30, "K30: -mV");

    g.lbl_k15 = lv_label_create(row);
    lv_label_set_text(g.lbl_k15, "K15: -mV");

    /* Brightness */
    lv_obj_t *lbl_slider = lv_label_create(col);
    lv_label_set_text(lbl_slider, "Display brightness");

    g.slider = lv_slider_create(col);
    lv_slider_set_range(g.slider, 10, 100);
    lv_slider_set_value(g.slider, read_current_brightness(), LV_ANIM_OFF);
    lv_obj_set_width(g.slider, lv_pct(100));
    lv_obj_add_event_cb(g.slider, slider_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Info text */
    lv_obj_t *info = lv_label_create(col);
    lv_label_set_text(info,
        "GOcontroll Moduline Display HMI demo\n"
        "https://github.com/GOcontroll/GOcontroll-HMI-Demo\n\n"
        "Default Users: root:root (no ssh) GOcontroll:Moduline");
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);

    /* Exit button */
    lv_obj_t *btn = lv_button_create(col);
    lv_obj_set_style_pad_hor(btn, 60, 0);
    lv_obj_add_event_cb(btn, exit_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn = lv_label_create(btn);
    lv_label_set_text(lbl_btn, "Exit demo");
    lv_obj_center(lbl_btn);

    /* Voltage poll timer: 500ms on the main loop thread — no locking needed. */
    g.voltage_timer = lv_timer_create(voltage_timer_cb, 500, NULL);
    /* Fire once immediately so the UI shows real values on first paint. */
    lv_timer_ready(g.voltage_timer);
}

void ui_shutdown(void)
{
    if (g.voltage_timer) {
        lv_timer_delete(g.voltage_timer);
        g.voltage_timer = NULL;
    }
}

int ui_should_exit(void)
{
    return g.should_exit;
}
