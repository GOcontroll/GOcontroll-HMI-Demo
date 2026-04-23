#ifndef GOCONTROLL_HMI_SPLASH_DRM_H
#define GOCONTROLL_HMI_SPLASH_DRM_H

#include <stdint.h>

/* Minimal DRM dumb-buffer wrapper for the boot splash. Double-buffered
 * ping-pong; the caller renders into the back buffer and calls commit to
 * flip it to scan-out. No LVGL, no atomic-modeset paraphernalia — just
 * enough to paint ~30 splash frames and then idle on the final one. */
typedef struct splash_drm splash_drm_t;

splash_drm_t *splash_drm_open(const char *device);
void          splash_drm_close(splash_drm_t *d);

uint32_t splash_drm_width (splash_drm_t *d);
uint32_t splash_drm_height(splash_drm_t *d);
uint32_t splash_drm_stride(splash_drm_t *d);

/* Pointer to the currently-inactive buffer's mmap. Write-combined memory:
 * writes are fast (~10 GB/s on i.MX 8M Plus), reads are not. Treat it as
 * write-only. Don't free; owned by splash_drm. */
uint8_t *splash_drm_back_buffer(splash_drm_t *d);

/* Present the back buffer on the display and swap roles. Returns 0 on
 * success, -1 on failure. */
int splash_drm_commit(splash_drm_t *d);

#endif
