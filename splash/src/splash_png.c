#define _GNU_SOURCE
#include "splash_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>

splash_image_t *splash_png_load(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    uint8_t sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0) {
        fclose(fp);
        return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return NULL; }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    /* libpng uses setjmp for error recovery. */
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 w = 0, h = 0;
    int depth = 0, ctype = 0;
    png_get_IHDR(png, info, &w, &h, &depth, &ctype, NULL, NULL, NULL);

    /* Normalise to 8-bit RGBA regardless of source bit depth / type. */
    if (depth == 16)                                    png_set_strip_16(png);
    if (ctype == PNG_COLOR_TYPE_PALETTE)                png_set_palette_to_rgb(png);
    if (ctype == PNG_COLOR_TYPE_GRAY && depth < 8)      png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))        png_set_tRNS_to_alpha(png);
    if (ctype == PNG_COLOR_TYPE_GRAY ||
        ctype == PNG_COLOR_TYPE_GRAY_ALPHA)             png_set_gray_to_rgb(png);
    if (ctype == PNG_COLOR_TYPE_RGB ||
        ctype == PNG_COLOR_TYPE_GRAY ||
        ctype == PNG_COLOR_TYPE_PALETTE)
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png, info);

    size_t row_bytes = png_get_rowbytes(png, info);
    /* row_bytes should now equal w*4, but belt-and-braces. */
    if (row_bytes < (size_t)w * 4) row_bytes = (size_t)w * 4;

    splash_image_t *img = calloc(1, sizeof(*img));
    if (!img) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    img->w = w;
    img->h = h;
    img->rgba = malloc(row_bytes * h);
    if (!img->rgba) {
        free(img);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_bytep *rows = malloc(sizeof(png_bytep) * h);
    if (!rows) {
        free(img->rgba);
        free(img);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    for (png_uint_32 y = 0; y < h; y++)
        rows[y] = img->rgba + (size_t)y * row_bytes;

    png_read_image(png, rows);
    png_read_end(png, NULL);

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return img;
}

void splash_png_free(splash_image_t *img)
{
    if (!img) return;
    free(img->rgba);
    free(img);
}
