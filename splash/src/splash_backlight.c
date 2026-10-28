#define _GNU_SOURCE
#include "splash_backlight.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BACKLIGHT_CLASS     "/sys/class/backlight"
/* Brightness the fade-in ends at, in percent of max_brightness. Overridable
 * with GOCONTROLL_SPLASH_BRIGHTNESS so an integrator can match their own HMI
 * without rebuilding. */
#define DEFAULT_TARGET_PCT  50
/* Perceived brightness is far from linear in PWM duty: the step from 0 to a
 * few percent looks like switching on. Raising progress to this power keeps
 * the start of the ramp soft and makes the whole ramp look even. */
#define PERCEPTUAL_GAMMA    2.2f

static int g_fd = -1;
static long g_target;
static long g_last = -1;

static long read_long(const char *dir, const char *name)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static long target_percent(void)
{
    const char *env = getenv("GOCONTROLL_SPLASH_BRIGHTNESS");
    if (env && *env) {
        char *end;
        long v = strtol(env, &end, 10);
        if (*end == '\0' && v >= 0 && v <= 100) return v;
        fprintf(stderr, "splash: ignoring GOCONTROLL_SPLASH_BRIGHTNESS=%s\n", env);
    }
    return DEFAULT_TARGET_PCT;
}

bool splash_backlight_open(void)
{
    DIR *d = opendir(BACKLIGHT_CLASS);
    if (!d) return false;

    char dir[PATH_MAX] = "";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(dir, sizeof(dir), "%s/%s", BACKLIGHT_CLASS, e->d_name);
        break;
    }
    closedir(d);
    if (!dir[0]) return false;

    long max = read_long(dir, "max_brightness");
    long cur = read_long(dir, "brightness");
    if (max <= 0 || cur < 0) return false;

    /* Someone already lit the panel (systemd-backlight restore, or an older
     * kernel whose device tree default is not 0). Taking it back to 0 for a
     * fade-in would only add a flicker. */
    if (cur > 0) return false;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/brightness", dir);
    g_fd = open(path, O_WRONLY | O_CLOEXEC);
    if (g_fd < 0) {
        perror("splash: open backlight");
        return false;
    }
    g_target = (max * target_percent() + 50) / 100;
    g_last = 0;
    return true;
}

void splash_backlight_set(float level)
{
    if (g_fd < 0) return;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;

    long v = (long)(g_target * powf(level, PERCEPTUAL_GAMMA) + 0.5f);
    if (v == g_last) return;        /* every write is an I2C transfer */

    char buf[24];
    int n = snprintf(buf, sizeof(buf), "%ld", v);
    if (pwrite(g_fd, buf, (size_t)n, 0) == n) g_last = v;
}

void splash_backlight_close(void)
{
    if (g_fd >= 0) close(g_fd);
    g_fd = -1;
}
