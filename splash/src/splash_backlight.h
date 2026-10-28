#ifndef SPLASH_BACKLIGHT_H
#define SPLASH_BACKLIGHT_H

#include <stdbool.h>

/* The kernel leaves the backlight at 0 until userspace has a picture to show,
 * so the panel does not light up on an empty frame. The splash owns that first
 * switch-on: it fades the backlight in together with the logo.
 *
 * Opens the first device under /sys/class/backlight. Returns false when there
 * is none, or when something earlier in the boot (e.g. systemd-backlight)
 * already turned it on — in both cases the splash leaves it alone. */
bool splash_backlight_open(void);

/* Set the backlight for ramp progress `level` (0..1). The progress goes
 * through a perceptual curve, so a linear 0..1 looks like an even fade from
 * dark to the target brightness. No-op when splash_backlight_open() returned
 * false. */
void splash_backlight_set(float level);

void splash_backlight_close(void);

#endif
