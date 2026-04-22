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
#include "src/drivers/evdev/lv_evdev.h"

#include "config.h"
#include "drm_display.h"
#include "ipc.h"
#include "settings.h"
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

/* LVGL flush: bridge from the cached shadow buffer into the write-combined
 * DRM scanout, rotating 180° in the copy. Doing it here — and not as an
 * in-place post-render pass on the DRM buffer — avoids read-modify-write
 * against WC memory, which is ~50x slower than write-only streaming. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    drm_display_t *drm = lv_display_get_user_data(disp);
    uint32_t src_stride = lv_display_get_horizontal_resolution(disp) * sizeof(uint32_t);
    drm_display_present_rotated180(drm, px_map, src_stride);
    lv_display_flush_ready(disp);
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

    /* --- DRM display --------------------------------------------------- */
    drm_display_t *drm = drm_display_open(DRM_DEVICE);
    if (!drm) {
        fprintf(stderr, "drm_display_open(%s) failed\n", DRM_DEVICE);
        return 1;
    }

    uint32_t hres   = drm_display_width(drm);
    uint32_t vres   = drm_display_height(drm);
    uint32_t stride = hres * sizeof(uint32_t);
    size_t   shadow_size = (size_t)stride * vres;

    /* Cached shadow buffer for LVGL to render into. Cached reads (~1.7 GB/s)
     * feed the present-time rotate+copy into the WC DRM buffer — one full
     * screen pass takes ~2 ms at 1280x720. */
    uint8_t *shadow = aligned_alloc(64, shadow_size);
    if (!shadow) {
        fprintf(stderr, "failed to allocate %zu byte shadow buffer\n", shadow_size);
        drm_display_close(drm);
        return 1;
    }
    memset(shadow, 0, shadow_size);

    lv_display_t *disp = lv_display_create(hres, vres);
    if (!disp) {
        fprintf(stderr, "lv_display_create failed\n");
        free(shadow);
        drm_display_close(drm);
        return 1;
    }
    lv_display_set_user_data(disp, drm);
    lv_display_set_dpi(disp, drm_display_dpi(drm));
    /* Single-buffered LVGL: the shadow is the only render target. Page-flip
     * ping-pong happens inside the DRM wrapper, so LVGL never renders into a
     * buffer that's currently being scanned out (which was causing flicker
     * during slider drag under the old 2-buffer DIRECT setup). */
    lv_display_set_buffers_with_stride(disp, shadow, NULL, shadow_size, stride,
                                       LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    /* --- Touch (cyttsp6_mt via evdev, rotated to match display) ------- */
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, TOUCH_DEVICE);
    if (touch) {
        lv_indev_set_display(touch, disp);
        /* The panel is mounted 180° rotated, so the touch IC reports raw
         * coordinates in the panel's native (upside-down) frame. Invert both
         * axes to get user-frame coords that match the rotated scanout.
         * Same transform as libinput's LIBINPUT_CALIBRATION_MATRIX "-1 0 1 0 -1 1". */
        lv_evdev_set_calibration(touch, (int32_t)hres - 1, (int32_t)vres - 1, 0, 0);
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

    hmi_settings_t settings;
    hmi_settings_load(&settings);

    /* --- UI construction ---------------------------------------------- */
    ui_build(demo_pid, &cfg, ASSET_DIR, &settings);

    /* --- Main loop ---------------------------------------------------- */
    while (!g_stop && !ui_should_exit()) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 50) sleep_ms = 50;   /* cap latency for touch */
        usleep(sleep_ms * 1000u);
    }

    ui_shutdown();
    lv_deinit();
    drm_display_close(drm);
    free(shadow);
    return 0;
}
