#define _GNU_SOURCE
#include "drm_display.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define BUFFER_CNT 2

typedef struct {
    uint32_t handle;
    uint32_t pitch;
    uint32_t offset;
    size_t   size;
    uint8_t *map;
    uint32_t fb_id;
} drm_buffer_t;

struct drm_display_t {
    int fd;

    uint32_t conn_id, enc_id, crtc_id, plane_id;
    drmModeModeInfo mode;
    uint32_t blob_id;
    uint32_t mm_width, mm_height;

    drm_buffer_t bufs[BUFFER_CNT];
    int          back_idx;     /* buffer to write into on next present */
    int          first_commit; /* next commit must include modeset */
    int          flip_pending; /* a page-flip event has not been handled yet */

    struct {
        uint32_t fb_id, crtc_id;
        uint32_t src_x, src_y, src_w, src_h;
        uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
    } plane_prop;
    struct {
        uint32_t mode_id, active;
    } crtc_prop;
    struct {
        uint32_t crtc_id;
    } conn_prop;

    drmEventContext evctx;
};

static void log_err(const char *msg)
{
    fprintf(stderr, "drm_display: %s: %s\n", msg, strerror(errno));
}

static uint32_t find_prop(int fd, uint32_t obj_id, uint32_t obj_type, const char *name)
{
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) return 0;

    uint32_t ret = 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p) continue;
        if (strcmp(p->name, name) == 0) ret = p->prop_id;
        drmModeFreeProperty(p);
        if (ret) break;
    }
    drmModeFreeObjectProperties(props);
    return ret;
}

static int find_connected_connector(drm_display_t *d, drmModeRes *res)
{
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(d->fd, res->connectors[i]);
        if (!c) continue;
        if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            d->conn_id   = c->connector_id;
            d->mode      = c->modes[0];
            d->mm_width  = c->mmWidth;
            d->mm_height = c->mmHeight;
            d->enc_id    = c->encoder_id;
            drmModeFreeConnector(c);
            return 0;
        }
        drmModeFreeConnector(c);
    }
    return -1;
}

static int find_crtc(drm_display_t *d, drmModeRes *res, uint32_t *crtc_idx_out)
{
    drmModeEncoder *enc = drmModeGetEncoder(d->fd, d->enc_id);
    if (enc && enc->crtc_id) {
        d->crtc_id = enc->crtc_id;
        drmModeFreeEncoder(enc);
        for (int i = 0; i < res->count_crtcs; i++)
            if (res->crtcs[i] == d->crtc_id) { *crtc_idx_out = i; return 0; }
    }
    if (enc) drmModeFreeEncoder(enc);

    /* Fall back: pick any CRTC this encoder can drive. */
    drmModeConnector *c = drmModeGetConnector(d->fd, d->conn_id);
    if (!c) return -1;
    for (int i = 0; i < c->count_encoders; i++) {
        drmModeEncoder *e = drmModeGetEncoder(d->fd, c->encoders[i]);
        if (!e) continue;
        for (int j = 0; j < res->count_crtcs; j++) {
            if (!(e->possible_crtcs & (1u << j))) continue;
            d->enc_id  = e->encoder_id;
            d->crtc_id = res->crtcs[j];
            *crtc_idx_out = j;
            drmModeFreeEncoder(e);
            drmModeFreeConnector(c);
            return 0;
        }
        drmModeFreeEncoder(e);
    }
    drmModeFreeConnector(c);
    return -1;
}

static int find_plane(drm_display_t *d, uint32_t crtc_idx)
{
    drmModePlaneResPtr planes = drmModeGetPlaneResources(d->fd);
    if (!planes) return -1;

    int ret = -1;
    for (uint32_t i = 0; i < planes->count_planes; i++) {
        drmModePlanePtr p = drmModeGetPlane(d->fd, planes->planes[i]);
        if (!p) continue;
        if (!(p->possible_crtcs & (1u << crtc_idx))) { drmModeFreePlane(p); continue; }
        /* Prefer the primary plane; XRGB8888 support is mandatory. */
        int has_xr24 = 0;
        for (uint32_t j = 0; j < p->count_formats; j++)
            if (p->formats[j] == DRM_FORMAT_XRGB8888) { has_xr24 = 1; break; }
        if (!has_xr24) { drmModeFreePlane(p); continue; }

        /* Check plane type == primary if property exists. */
        uint64_t type_val = 0;
        uint32_t type_prop = find_prop(d->fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type");
        if (type_prop) {
            drmModeObjectPropertiesPtr oprops =
                drmModeObjectGetProperties(d->fd, p->plane_id, DRM_MODE_OBJECT_PLANE);
            if (oprops) {
                for (uint32_t k = 0; k < oprops->count_props; k++)
                    if (oprops->props[k] == type_prop) { type_val = oprops->prop_values[k]; break; }
                drmModeFreeObjectProperties(oprops);
            }
        }
        if (type_prop && type_val != DRM_PLANE_TYPE_PRIMARY) { drmModeFreePlane(p); continue; }

        d->plane_id = p->plane_id;
        drmModeFreePlane(p);
        ret = 0;
        break;
    }
    drmModeFreePlaneResources(planes);
    return ret;
}

static int load_prop_ids(drm_display_t *d)
{
    d->plane_prop.fb_id   = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    d->plane_prop.crtc_id = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    d->plane_prop.src_x   = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    d->plane_prop.src_y   = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    d->plane_prop.src_w   = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    d->plane_prop.src_h   = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    d->plane_prop.crtc_x  = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    d->plane_prop.crtc_y  = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    d->plane_prop.crtc_w  = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    d->plane_prop.crtc_h  = find_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");

    d->crtc_prop.mode_id = find_prop(d->fd, d->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    d->crtc_prop.active  = find_prop(d->fd, d->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");

    d->conn_prop.crtc_id = find_prop(d->fd, d->conn_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");

    return (d->plane_prop.fb_id && d->plane_prop.crtc_id && d->plane_prop.src_w &&
            d->plane_prop.crtc_w && d->crtc_prop.mode_id && d->crtc_prop.active &&
            d->conn_prop.crtc_id) ? 0 : -1;
}

static int alloc_dumb_fb(drm_display_t *d, drm_buffer_t *b)
{
    struct drm_mode_create_dumb cr = {
        .width  = d->mode.hdisplay,
        .height = d->mode.vdisplay,
        .bpp    = 32,
    };
    if (drmIoctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &cr) < 0) {
        log_err("CREATE_DUMB"); return -1;
    }
    b->handle = cr.handle;
    b->pitch  = cr.pitch;
    b->size   = cr.size;

    struct drm_mode_map_dumb mr = { .handle = cr.handle };
    if (drmIoctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &mr) < 0) {
        log_err("MAP_DUMB"); return -1;
    }
    b->offset = mr.offset;

    b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, mr.offset);
    if (b->map == MAP_FAILED) { log_err("mmap"); return -1; }
    memset(b->map, 0, b->size);

    uint32_t handles[4] = { cr.handle };
    uint32_t pitches[4] = { cr.pitch };
    uint32_t offsets[4] = { 0 };
    if (drmModeAddFB2(d->fd, d->mode.hdisplay, d->mode.vdisplay, DRM_FORMAT_XRGB8888,
                      handles, pitches, offsets, &b->fb_id, 0) < 0) {
        log_err("AddFB2"); return -1;
    }
    return 0;
}

static void free_dumb_fb(drm_display_t *d, drm_buffer_t *b)
{
    if (b->fb_id)   { drmModeRmFB(d->fd, b->fb_id); b->fb_id = 0; }
    if (b->map != NULL && b->map != MAP_FAILED) { munmap(b->map, b->size); b->map = NULL; }
    if (b->handle)  {
        struct drm_mode_destroy_dumb dd = { .handle = b->handle };
        drmIoctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        b->handle = 0;
    }
}

static void page_flip_cb(int fd, unsigned seq, unsigned sec, unsigned usec, void *user)
{
    (void)fd; (void)seq; (void)sec; (void)usec;
    drm_display_t *d = user;
    d->flip_pending = 0;
}

static void wait_for_flip(drm_display_t *d)
{
    if (!d->flip_pending) return;
    struct pollfd pfd = { .fd = d->fd, .events = POLLIN };
    while (d->flip_pending) {
        int r;
        do { r = poll(&pfd, 1, -1); } while (r == -1 && errno == EINTR);
        if (r > 0) drmHandleEvent(d->fd, &d->evctx);
        else       { log_err("poll"); return; }
    }
}

static int submit_flip(drm_display_t *d, drm_buffer_t *buf)
{
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) return -1;

    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

    if (d->first_commit) {
        drmModeAtomicAddProperty(req, d->conn_id, d->conn_prop.crtc_id, d->crtc_id);
        drmModeAtomicAddProperty(req, d->crtc_id, d->crtc_prop.mode_id, d->blob_id);
        drmModeAtomicAddProperty(req, d->crtc_id, d->crtc_prop.active,  1);
        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
    }

    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.fb_id,   buf->fb_id);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.crtc_id, d->crtc_id);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.src_x,   0);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.src_y,   0);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.src_w,   (uint64_t)d->mode.hdisplay << 16);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.src_h,   (uint64_t)d->mode.vdisplay << 16);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.crtc_x,  0);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.crtc_y,  0);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.crtc_w,  d->mode.hdisplay);
    drmModeAtomicAddProperty(req, d->plane_id, d->plane_prop.crtc_h,  d->mode.vdisplay);

    int ret = drmModeAtomicCommit(d->fd, req, flags, d);
    drmModeAtomicFree(req);
    if (ret) { log_err("AtomicCommit"); return ret; }

    d->flip_pending = 1;
    d->first_commit = 0;
    return 0;
}

drm_display_t *drm_display_open(const char *device_path)
{
    drm_display_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->fd = -1;
    d->first_commit = 1;

    d->fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (d->fd < 0) { log_err("open"); goto fail; }

    uint64_t has_dumb = 0;
    if (drmGetCap(d->fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 || !has_dumb) {
        fprintf(stderr, "drm_display: no dumb buffer support\n"); goto fail;
    }
    if (drmSetClientCap(d->fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
        log_err("set ATOMIC cap"); goto fail;
    }
    if (drmSetClientCap(d->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) < 0) {
        log_err("set UNIVERSAL_PLANES cap"); goto fail;
    }

    drmModeRes *res = drmModeGetResources(d->fd);
    if (!res) { log_err("GetResources"); goto fail; }

    if (find_connected_connector(d, res) < 0) {
        fprintf(stderr, "drm_display: no connected connector\n");
        drmModeFreeResources(res); goto fail;
    }

    uint32_t crtc_idx = 0;
    if (find_crtc(d, res, &crtc_idx) < 0) {
        fprintf(stderr, "drm_display: no usable CRTC\n");
        drmModeFreeResources(res); goto fail;
    }
    if (find_plane(d, crtc_idx) < 0) {
        fprintf(stderr, "drm_display: no suitable primary plane\n");
        drmModeFreeResources(res); goto fail;
    }
    drmModeFreeResources(res);

    if (load_prop_ids(d) < 0) {
        fprintf(stderr, "drm_display: missing required properties\n"); goto fail;
    }

    if (drmModeCreatePropertyBlob(d->fd, &d->mode, sizeof(d->mode), &d->blob_id) < 0) {
        log_err("CreatePropertyBlob"); goto fail;
    }

    for (int i = 0; i < BUFFER_CNT; i++)
        if (alloc_dumb_fb(d, &d->bufs[i]) < 0) goto fail;

    d->evctx.version = DRM_EVENT_CONTEXT_VERSION;
    d->evctx.page_flip_handler = page_flip_cb;
    d->back_idx = 0;
    return d;

fail:
    drm_display_close(d);
    return NULL;
}

void drm_display_close(drm_display_t *d)
{
    if (!d) return;
    if (d->fd >= 0) {
        wait_for_flip(d);
        for (int i = 0; i < BUFFER_CNT; i++) free_dumb_fb(d, &d->bufs[i]);
        if (d->blob_id) drmModeDestroyPropertyBlob(d->fd, d->blob_id);
        close(d->fd);
    }
    free(d);
}

uint32_t drm_display_width (const drm_display_t *d) { return d->mode.hdisplay; }
uint32_t drm_display_height(const drm_display_t *d) { return d->mode.vdisplay; }
uint32_t drm_display_stride(const drm_display_t *d) { return d->bufs[0].pitch; }
uint32_t drm_display_dpi   (const drm_display_t *d)
{
    if (!d->mm_width) return 130;
    return (uint32_t)((d->mode.hdisplay * 25.4f) / d->mm_width + 0.5f);
}

/* Rotate 180° while copying src -> dst. Writes go forward in dst memory for
 * friendly write-combining into the WC DRM mapping. Scalar aarch64 is fine
 * here (~2 ms at 1280x720); NEON shaves ~15% but isn't worth the complexity. */
static void rotate180_copy(uint8_t *dst, uint32_t dst_stride,
                           const uint8_t *src, uint32_t src_stride,
                           int w, int h)
{
    for (int y = 0; y < h; y++) {
        const uint32_t *s = (const uint32_t *)(src + (size_t)(h - 1 - y) * src_stride);
        uint32_t       *d = (uint32_t *)      (dst + (size_t)y * dst_stride);
        for (int x = 0; x < w; x++) d[x] = s[w - 1 - x];
    }
}

void drm_display_present_rotated180(drm_display_t *d,
                                    const uint8_t *src, uint32_t src_stride)
{
    /* Wait until the previous flip is off the back buffer before touching it. */
    wait_for_flip(d);

    drm_buffer_t *back = &d->bufs[d->back_idx];
    rotate180_copy(back->map, back->pitch, src, src_stride,
                   d->mode.hdisplay, d->mode.vdisplay);

    if (submit_flip(d, back) != 0) return;
    d->back_idx ^= 1;
}
