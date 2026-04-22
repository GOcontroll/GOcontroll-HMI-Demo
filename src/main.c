#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "src/drivers/display/drm/lv_linux_drm.h"
#include "src/drivers/evdev/lv_evdev.h"

#include "config.h"
#include "ipc.h"
#include "ui.h"

#define DRM_DEVICE         "/dev/dri/card1"
#define TOUCH_DEVICE       "/dev/input/event0"
#define ASSET_DIR          "/usr/share/gocontroll-hmi-demo"
#define BACKLIGHT_SYSFS    "/sys/class/backlight/max25014/brightness"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* LVGL tick source — monotonic millisecond counter. */
static uint32_t tick_get_ms_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* lv_linux_drm hardcodes LV_DISPLAY_RENDER_MODE_DIRECT, which makes
 * lv_display_set_rotation() a no-op. We instead switch to FULL mode and
 * rotate the buffer 180° in-place right before each page-flip. FULL mode
 * re-renders the whole buffer every frame, so there's no cross-buffer sync
 * to worry about. 32bpp only — lv_conf.h pins LV_COLOR_DEPTH=32. */
static void rotate180_flush_start_cb(lv_event_t *e)
{
    lv_display_t *disp = lv_event_get_current_target(e);
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    if (!buf) return;

    uint8_t  *base   = buf->data;
    int32_t   w      = (int32_t)buf->header.w;
    int32_t   h      = (int32_t)buf->header.h;
    uint32_t  stride = buf->header.stride;

    for (int32_t y = 0; y < h / 2; y++) {
        uint32_t *top = (uint32_t *)(base + (uint32_t)y * stride);
        uint32_t *bot = (uint32_t *)(base + (uint32_t)(h - 1 - y) * stride);
        for (int32_t x = 0; x < w; x++) {
            uint32_t tmp = top[x];
            top[x] = bot[w - 1 - x];
            bot[w - 1 - x] = tmp;
        }
    }
    if (h & 1) {
        uint32_t *mid = (uint32_t *)(base + (uint32_t)(h / 2) * stride);
        for (int32_t x = 0; x < w / 2; x++) {
            uint32_t tmp = mid[x];
            mid[x] = mid[w - 1 - x];
            mid[w - 1 - x] = tmp;
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    /* Don't die if stdout pipe closes (e.g. journald restart) */
    signal(SIGPIPE, SIG_IGN);

    /* --- LVGL init ---------------------------------------------------- */
    lv_init();
    lv_tick_set_cb(tick_get_ms_cb);

    /* --- Display (KMS/DRM, rotate 180°) ------------------------------- */
    lv_display_t *disp = lv_linux_drm_create();
    if (!disp) {
        fprintf(stderr, "lv_linux_drm_create() failed\n");
        return 1;
    }
    if (lv_linux_drm_set_file(disp, DRM_DEVICE, -1) != LV_RESULT_OK) {
        fprintf(stderr, "lv_linux_drm_set_file(%s) failed\n", DRM_DEVICE);
        return 1;
    }
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_add_event_cb(disp, rotate180_flush_start_cb, LV_EVENT_FLUSH_START, NULL);

    /* --- Touch (cyttsp6_mt via evdev, rotated to match display) ------- */
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, TOUCH_DEVICE);
    if (touch) {
        lv_indev_set_display(touch, disp);
        /* The panel is mounted 180° rotated, so the touch IC reports raw
         * coordinates in the panel's native (upside-down) frame. Invert both
         * axes to get user-frame coords that match the rotated scanout.
         * Same transform as libinput's LIBINPUT_CALIBRATION_MATRIX "-1 0 1 0 -1 1". */
        lv_evdev_set_calibration(touch, 1279, 719, 0, 0);
    } else {
        fprintf(stderr, "warning: could not open touch device %s: %s\n",
                TOUCH_DEVICE, strerror(errno));
    }

    /* --- Simulink IPC target ------------------------------------------ */
    pid_t demo_pid = ipc_find_pid("gocontroll_demo");
    if (demo_pid < 0) {
        fprintf(stderr, "warning: gocontroll_demo not running; voltages unavailable\n");
    }

    hmi_config_t cfg;
    hmi_config_load(&cfg, ASSET_DIR);

    /* --- UI construction ---------------------------------------------- */
    ui_build(demo_pid, &cfg, ASSET_DIR);

    /* --- Main loop ---------------------------------------------------- */
    while (!g_stop && !ui_should_exit()) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 50) sleep_ms = 50;   /* cap latency for touch */
        usleep(sleep_ms * 1000u);
    }

    ui_shutdown();
    lv_deinit();
    return 0;
}
