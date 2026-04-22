#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

/* Minimal KMS/DRM wrapper tuned for the i.MX 8M Plus LCDIF + LVDS panel.
 *
 * Owns two dumb scanout buffers and ping-pongs page-flips between them, while
 * the caller renders into its own cached shadow buffer. Presenting rotates the
 * shadow 180° into the back buffer in a single write-only pass into
 * write-combined scanout memory — ~2 ms at 1280x720, versus ~110 ms for the
 * old read-modify-write in-place rotation. */

typedef struct drm_display_t drm_display_t;

drm_display_t *drm_display_open(const char *device_path);
void           drm_display_close(drm_display_t *d);

uint32_t drm_display_width(const drm_display_t *d);
uint32_t drm_display_height(const drm_display_t *d);
uint32_t drm_display_stride(const drm_display_t *d);
uint32_t drm_display_dpi(const drm_display_t *d);

/* Copy the caller's XRGB8888 src buffer into the next DRM back buffer,
 * rotated 180°, then submit a non-blocking atomic page-flip. Blocks on any
 * in-flight flip first so the back buffer is guaranteed not on scanout. */
void drm_display_present_rotated180(drm_display_t *d,
                                    const uint8_t *src,
                                    uint32_t src_stride);

#endif
