#define _GNU_SOURCE
#include "splash_drm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

typedef struct {
    uint32_t id;      /* DRM framebuffer object id */
    uint32_t handle;  /* dumb buffer gem handle    */
    uint32_t stride;
    uint32_t size;
    uint8_t *map;
} db_t;

struct splash_drm {
    int          fd;
    drmModeRes  *res;
    uint32_t     connector_id;
    uint32_t     crtc_id;
    drmModeModeInfo mode;
    drmModeCrtc *saved_crtc;

    uint32_t     w, h, stride;

    db_t         buf[2];
    int          front;  /* index of buffer currently on screen */
};

static int create_dumb(int fd, uint32_t w, uint32_t h, db_t *b)
{
    struct drm_mode_create_dumb cdb = { .width = w, .height = h, .bpp = 32 };
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cdb) < 0) return -1;
    b->handle = cdb.handle;
    b->stride = cdb.pitch;
    b->size   = cdb.size;

    uint32_t handles[4] = { cdb.handle, 0, 0, 0 };
    uint32_t strides[4] = { cdb.pitch,  0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    if (drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888,
                      handles, strides, offsets, &b->id, 0) < 0) {
        struct drm_mode_destroy_dumb dd = { .handle = cdb.handle };
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        return -1;
    }

    struct drm_mode_map_dumb mm = { .handle = cdb.handle };
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mm) < 0) {
        drmModeRmFB(fd, b->id);
        struct drm_mode_destroy_dumb dd = { .handle = cdb.handle };
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        return -1;
    }

    b->map = mmap(NULL, cdb.size, PROT_WRITE, MAP_SHARED, fd, mm.offset);
    if (b->map == MAP_FAILED) {
        drmModeRmFB(fd, b->id);
        struct drm_mode_destroy_dumb dd = { .handle = cdb.handle };
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        return -1;
    }
    return 0;
}

static void destroy_dumb(int fd, db_t *b)
{
    if (b->map && b->map != MAP_FAILED) { munmap(b->map, b->size); b->map = NULL; }
    if (b->id)     { drmModeRmFB(fd, b->id); b->id = 0; }
    if (b->handle) {
        struct drm_mode_destroy_dumb dd = { .handle = b->handle };
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        b->handle = 0;
    }
}

splash_drm_t *splash_drm_open(const char *device)
{
    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) return NULL;

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { close(fd); return NULL; }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        drmModeFreeConnector(c);
    }
    if (!conn) { drmModeFreeResources(res); close(fd); return NULL; }

    drmModeModeInfo mode = conn->modes[0];

    uint32_t crtc_id = 0;
    drmModeEncoder *enc = conn->encoder_id ? drmModeGetEncoder(fd, conn->encoder_id) : NULL;
    if (enc && enc->crtc_id) {
        crtc_id = enc->crtc_id;
    } else {
        /* First CRTC in possible_crtcs of any encoder connected to this connector. */
        for (int i = 0; i < conn->count_encoders && !crtc_id; i++) {
            drmModeEncoder *e = drmModeGetEncoder(fd, conn->encoders[i]);
            if (!e) continue;
            for (int j = 0; j < res->count_crtcs; j++) {
                if (e->possible_crtcs & (1u << j)) { crtc_id = res->crtcs[j]; break; }
            }
            drmModeFreeEncoder(e);
        }
    }
    drmModeFreeEncoder(enc);

    if (!crtc_id) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        close(fd);
        return NULL;
    }

    splash_drm_t *d = calloc(1, sizeof(*d));
    if (!d) { drmModeFreeConnector(conn); drmModeFreeResources(res); close(fd); return NULL; }
    d->fd           = fd;
    d->res          = res;
    d->connector_id = conn->connector_id;
    d->crtc_id      = crtc_id;
    d->mode         = mode;
    d->w            = mode.hdisplay;
    d->h            = mode.vdisplay;
    d->saved_crtc   = drmModeGetCrtc(fd, crtc_id);

    drmModeFreeConnector(conn);

    if (create_dumb(fd, d->w, d->h, &d->buf[0]) != 0 ||
        create_dumb(fd, d->w, d->h, &d->buf[1]) != 0) {
        splash_drm_close(d);
        return NULL;
    }
    d->stride = d->buf[0].stride;

    /* Zero both so a premature exit leaves a clean black frame. */
    memset(d->buf[0].map, 0, d->buf[0].size);
    memset(d->buf[1].map, 0, d->buf[1].size);

    if (drmModeSetCrtc(fd, crtc_id, d->buf[0].id, 0, 0,
                       &d->connector_id, 1, &d->mode) < 0) {
        splash_drm_close(d);
        return NULL;
    }
    d->front = 0;

    return d;
}

void splash_drm_close(splash_drm_t *d)
{
    if (!d) return;
    if (d->saved_crtc) {
        /* Best-effort restore of the previous CRTC setup on exit so fbcon
         * can repaint once the splash master releases. */
        drmModeSetCrtc(d->fd, d->saved_crtc->crtc_id, d->saved_crtc->buffer_id,
                       d->saved_crtc->x, d->saved_crtc->y,
                       &d->connector_id, 1, &d->saved_crtc->mode);
        drmModeFreeCrtc(d->saved_crtc);
        d->saved_crtc = NULL;
    }
    destroy_dumb(d->fd, &d->buf[0]);
    destroy_dumb(d->fd, &d->buf[1]);
    if (d->res) { drmModeFreeResources(d->res); d->res = NULL; }
    if (d->fd >= 0) { close(d->fd); d->fd = -1; }
    free(d);
}

uint32_t splash_drm_width (splash_drm_t *d) { return d->w; }
uint32_t splash_drm_height(splash_drm_t *d) { return d->h; }
uint32_t splash_drm_stride(splash_drm_t *d) { return d->stride; }

uint8_t *splash_drm_back_buffer(splash_drm_t *d)
{
    return d->buf[1 - d->front].map;
}

int splash_drm_commit(splash_drm_t *d)
{
    int back = 1 - d->front;
    /* Re-issuing drmModeSetCrtc with the same mode is effectively a buffer
     * swap — simpler than page-flip event handling and fast enough for the
     * ~30-frame splash animation. */
    if (drmModeSetCrtc(d->fd, d->crtc_id, d->buf[back].id, 0, 0,
                       &d->connector_id, 1, &d->mode) < 0)
        return -1;
    d->front = back;
    return 0;
}
