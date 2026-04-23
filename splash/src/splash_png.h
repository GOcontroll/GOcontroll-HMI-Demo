#ifndef GOCONTROLL_HMI_SPLASH_PNG_H
#define GOCONTROLL_HMI_SPLASH_PNG_H

#include <stdint.h>

/* Tightly-packed RGBA8888 image loaded from disk. Owns the pixel buffer. */
typedef struct {
    uint32_t  w, h;
    uint8_t  *rgba;   /* w*h*4 bytes, row-major, non-premultiplied */
} splash_image_t;

/* Load a PNG file and return a newly-allocated RGBA image. NULL on error. */
splash_image_t *splash_png_load(const char *path);

/* Release an image loaded by splash_png_load. Safe to call with NULL. */
void splash_png_free(splash_image_t *img);

#endif
