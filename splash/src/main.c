#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "splash_backlight.h"
#include "splash_drm.h"
#include "splash_png.h"

#define DRM_DEVICE       "/dev/dri/card1"
#define SPLASH_PNG       "/usr/share/gocontroll-hmi-demo/GOcontroll_splash.png"
#define ANIM_FRAMES      48
#define FADE_OUT_FRAMES  18          /* ~300 ms fade-out on SIGTERM */
#define FRAME_US         16666      /* ~60 Hz — animation runs ~800 ms */
#define SCALE_START      0.30f
#define SCALE_END        1.00f
/* The kernel can take up to ~10 s to bring the display chain up when i2c-3 is
 * busy after a short power interruption; card1 only exists after that. */
#define DRM_WAIT_MS      15000

static volatile sig_atomic_t g_stop = 0;

/* We start very early in sysinit and nothing orders us behind the DRM device,
 * so wait for it here rather than fail and show no splash at all. */
static int wait_for_device(const char *path, int timeout_ms)
{
    for (int waited = 0; access(path, F_OK) != 0; waited += 100) {
        if (waited >= timeout_ms) return -1;
        usleep(100 * 1000);
    }
    return 0;
}

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* Render one splash frame into the back buffer:
 *  - clear entire scan-out to black
 *  - blit the logo centred, scaled to (scale * native), alpha-faded
 *  - coordinates are pre-rotated 180 deg so the upside-down panel shows
 *    the logo upright (same trick drm_display.c uses for the main HMI).
 * dst is write-combined memory — never read from it. Each pixel is written
 * exactly once. */
static void render_frame(uint8_t *dst, uint32_t dst_w, uint32_t dst_h,
                         uint32_t stride, const splash_image_t *logo,
                         float scale, float alpha)
{
    memset(dst, 0, (size_t)stride * dst_h);

    if (alpha <= 0.0f || scale <= 0.0f) return;

    int lw = (int)logo->w;
    int lh = (int)logo->h;
    int sw = (int)(lw * scale + 0.5f);
    int sh = (int)(lh * scale + 0.5f);
    if (sw <= 0 || sh <= 0) return;

    int user_x0 = ((int)dst_w - sw) / 2;
    int user_y0 = ((int)dst_h - sh) / 2;

    uint32_t a8 = (uint32_t)(alpha * 255.0f + 0.5f);
    if (a8 > 255) a8 = 255;

    for (int oy = 0; oy < sh; oy++) {
        int src_y = (oy * lh) / sh;                         /* nearest neighbour */
        const uint8_t *src_row = logo->rgba + (size_t)src_y * (size_t)lw * 4;

        int user_y    = user_y0 + oy;
        int scanout_y = (int)dst_h - 1 - user_y;
        if (scanout_y < 0 || scanout_y >= (int)dst_h) continue;
        uint8_t *dst_row = dst + (size_t)scanout_y * stride;

        for (int ox = 0; ox < sw; ox++) {
            int src_x = (ox * lw) / sw;
            const uint8_t *p = src_row + (size_t)src_x * 4;
            uint32_t ea = ((uint32_t)p[3] * a8) / 255;      /* combined alpha 0..255 */

            int user_x    = user_x0 + ox;
            int scanout_x = (int)dst_w - 1 - user_x;
            if (scanout_x < 0 || scanout_x >= (int)dst_w) continue;
            uint8_t *dp = dst_row + (size_t)scanout_x * 4;

            /* Blend against the known-black background we just memset —
             * out = ea * src + (255 - ea) * 0 = ea * src. Avoids any
             * read-modify-write on WC memory. XRGB8888 little-endian:
             * byte[0]=B, [1]=G, [2]=R, [3]=X (ignored). */
            dp[0] = (uint8_t)(((uint32_t)p[2] * ea) / 255);
            dp[1] = (uint8_t)(((uint32_t)p[1] * ea) / 255);
            dp[2] = (uint8_t)(((uint32_t)p[0] * ea) / 255);
            dp[3] = 0xFF;
        }
    }
}

static void wait_signal(void)
{
    /* Hold the last rendered frame indefinitely — the display keeps showing
     * it because we don't page-flip away. Block the thread cheaply; systemd
     * will send SIGTERM when gocontroll-hmi-demo is about to take over. */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    int sig = 0;
    sigwait(&set, &sig);
    g_stop = 1;
}

/* Play a short fade-out animation (logo -> black) before we close DRM so
 * the handoff to the main HMI feels like a smooth transition rather than
 * a hard cut. Systemd's TimeoutStopSec must be generous enough (5 s is
 * plenty for ~300 ms of frames). */
static void play_fade_out(splash_drm_t *drm, const splash_image_t *logo,
                          uint32_t w, uint32_t h, uint32_t stride)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 1; i <= FADE_OUT_FRAMES; i++) {
        float t     = (float)i / (float)FADE_OUT_FRAMES;
        float eased = t * t;                /* ease-in quad */
        float alpha = 1.0f - eased;

        render_frame(splash_drm_back_buffer(drm), w, h, stride, logo, 1.0f, alpha);
        splash_drm_commit(drm);

        struct timespec target;
        target.tv_sec  = start.tv_sec;
        target.tv_nsec = start.tv_nsec + (long)i * FRAME_US * 1000L;
        while (target.tv_nsec >= 1000000000L) {
            target.tv_nsec -= 1000000000L;
            target.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);
    }

    /* Final fully-black frame so what's on screen when we release DRM
     * master is definitively black — the main HMI then fades in on top. */
    render_frame(splash_drm_back_buffer(drm), w, h, stride, logo, 1.0f, 0.0f);
    splash_drm_commit(drm);
}

int main(int argc, char **argv)
{
    const char *device = (argc > 1) ? argv[1] : DRM_DEVICE;
    const char *png    = (argc > 2) ? argv[2] : SPLASH_PNG;

    /* Block SIGTERM/SIGINT so sigwait() below delivers them to us and not
     * the default disposition (process termination). */
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGTERM);
    sigaddset(&block, SIGINT);
    sigprocmask(SIG_BLOCK, &block, NULL);

    signal(SIGPIPE, SIG_IGN);

    splash_image_t *logo = splash_png_load(png);
    if (!logo) {
        fprintf(stderr, "splash: cannot load %s: %s\n", png, strerror(errno));
        return 1;
    }

    if (wait_for_device(device, DRM_WAIT_MS) != 0) {
        fprintf(stderr, "splash: %s did not appear within %d ms\n", device, DRM_WAIT_MS);
        splash_png_free(logo);
        return 1;
    }

    splash_drm_t *drm = splash_drm_open(device);
    if (!drm) {
        fprintf(stderr, "splash: cannot open %s: %s\n", device, strerror(errno));
        splash_png_free(logo);
        return 1;
    }

    uint32_t w      = splash_drm_width(drm);
    uint32_t h      = splash_drm_height(drm);
    uint32_t stride = splash_drm_stride(drm);

    /* Opened only now: the panel must have a frame to show before it lights
     * up, and the first commit happens in the loop below. */
    splash_backlight_open();

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < ANIM_FRAMES; i++) {
        /* If we got SIGTERM mid-animation, bail out: render the final
         * state once so the user sees the full logo, then exit. */
        if (g_stop) break;

        float t      = (float)i / (float)(ANIM_FRAMES - 1);
        float inv    = 1.0f - t;
        float eased  = 1.0f - inv * inv * inv;           /* ease-out cubic */
        float scale  = SCALE_START + (SCALE_END - SCALE_START) * eased;
        float alpha  = eased;

        render_frame(splash_drm_back_buffer(drm), w, h, stride, logo, scale, alpha);
        splash_drm_commit(drm);
        /* Backlight follows the logo's own fade, so the panel never shows
         * an empty lit frame. */
        splash_backlight_set(eased);

        /* Pace to ~60 Hz against a monotonic baseline so rendering cost
         * doesn't accumulate into a slow animation. */
        struct timespec target;
        target.tv_sec  = start.tv_sec;
        target.tv_nsec = start.tv_nsec + (long)(i + 1) * FRAME_US * 1000L;
        while (target.tv_nsec >= 1000000000L) {
            target.tv_nsec -= 1000000000L;
            target.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);
    }

    /* Final frame at full state — guarantees the held picture is crisp
     * even if the animation loop exited early. */
    render_frame(splash_drm_back_buffer(drm), w, h, stride, logo, 1.0f, 1.0f);
    splash_drm_commit(drm);
    /* Left on when we exit: the HMI takes over from a black frame and
     * fades its own picture in under a lit panel. */
    splash_backlight_set(1.0f);
    splash_backlight_close();

    wait_signal();
    play_fade_out(drm, logo, w, h, stride);

    splash_drm_close(drm);
    splash_png_free(logo);
    return 0;
}
