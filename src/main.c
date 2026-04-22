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
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);

    /* --- Touch (cyttsp6_mt via evdev, rotated to match display) ------- */
    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, TOUCH_DEVICE);
    if (touch) {
        /* Pair the input with our display so LV_DISPLAY_ROTATION_180 applies. */
        lv_indev_set_display(touch, disp);
        /* Map raw touch range to display range and flip to cover 180°.
         * cyttsp6_mt on the 1280x720 Moduline reports 0..1279 / 0..719;
         * negating both axes is exactly what the udev
         * LIBINPUT_CALIBRATION_MATRIX "-1 0 1 0 -1 1" does for libinput. */
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
