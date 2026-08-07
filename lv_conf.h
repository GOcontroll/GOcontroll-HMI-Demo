/**
 * @file lv_conf.h
 * LVGL v9 configuration for GOcontroll Moduline HMI1 (i.MX 8M Plus)
 *
 * Tuned for:
 *   - ARM Cortex-A53 (NEON SIMD)
 *   - Vivante GC7000 via etnaviv (GLES 2.0 available, opt-in)
 *   - 1280x720 LVDS display, 16 bpp in memory is cheapest for the scanout bus
 *   - Single-threaded event loop (no worker threads touching the tree)
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH          32      /* XRGB8888 matches the DRM scanout format */

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD      33      /* ~30 Hz; cheaper than 60 and plenty for this UI */
#define LV_TICK_CUSTOM          0
#define LV_USE_TICK_GET         1

/*========================
   RENDERING CONFIGURATION
 *========================*/
/* Software renderer with NEON SIMD — the main perf win on A53 */
#define LV_USE_DRAW_SW          1
#define LV_DRAW_SW_SUPPORT_RGB565       1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB888       1
#define LV_DRAW_SW_SUPPORT_XRGB8888     1
#define LV_DRAW_SW_SUPPORT_ARGB8888     1
#define LV_DRAW_SW_SUPPORT_L8           0
#define LV_DRAW_SW_SUPPORT_AL88         0
#define LV_DRAW_SW_SUPPORT_A8           1

/* Use 1 parallel draw unit; this is a 4x A53 so extra units could help,
 * but the UI is small enough that scheduling overhead dominates. */
#define LV_DRAW_SW_DRAW_UNIT_CNT        1

/* Enable NEON acceleration. On aarch64 this is always available. */
#define LV_USE_DRAW_SW_ASM      LV_DRAW_SW_ASM_NEON

/* Optional GLES path — toggled from CMake via USE_OPENGLES */
#ifdef GOCONTROLL_USE_OPENGLES
#define LV_USE_OPENGLES         1
#define LV_USE_EGL              1
#define LV_USE_DRAW_OPENGLES    0   /* LVGL still rasterizes in SW; GPU only composites */
#else
#define LV_USE_OPENGLES         0
#define LV_USE_EGL              0
#endif

/*================
   MEMORY SETTINGS
 *================*/
#define LV_USE_BUILTIN_MALLOC   0   /* use libc malloc (glibc has good perf on aarch64) */

/*================
   LOG SETTINGS
 *================*/
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

/*====================
   DRIVER CONFIG
 *====================*/
#define LV_USE_LINUX_DRM        1
#define LV_LINUX_DRM_GPU_DEVICE "/dev/dri/card1"

#define LV_USE_EVDEV            1
#define LV_USE_LIBINPUT         0   /* we use raw evdev and rotate in code */

/*====================
   FILESYSTEM / IMAGES
 *====================*/
#define LV_USE_FS_POSIX         1
#define LV_FS_POSIX_LETTER      'A'
#define LV_FS_POSIX_PATH        ""
#define LV_FS_POSIX_CACHE_SIZE  0

#define LV_USE_LIBPNG           1   /* for loading GOcontroll.png at runtime */

#define LV_CACHE_DEF_SIZE       0
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0

/*================
   LAYOUTS
 *================*/
#define LV_USE_FLEX             1
#define LV_USE_GRID             0

/*================
   WIDGETS
 *================*/
#define LV_USE_ANIMIMG          0
#define LV_USE_ARC              1   /* Live Data K15/K30 gauge widgets */
#define LV_USE_BAR              1   /* slider depends on it; plus Pressure/SOC */
#define LV_USE_BUTTON           1
#define LV_USE_BUTTONMATRIX     0
#define LV_USE_CALENDAR         0
#define LV_USE_CANVAS           0
#define LV_USE_CHART            1   /* rolling sensor graphs on Information screen */
#define LV_USE_CHECKBOX         1   /* "show clock" toggle on Settings screen */
#define LV_USE_DROPDOWN         1   /* module selector on Configuration screen */
#define LV_USE_IMAGE            1
#define LV_USE_KEYBOARD         0
#define LV_USE_LABEL            1
#define LV_USE_LED              0
#define LV_USE_LIST             0
#define LV_USE_MENU             0
#define LV_USE_MSGBOX           0
#define LV_USE_ROLLER           0
#define LV_USE_SCALE            0
#define LV_USE_SLIDER           1
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_SWITCH           0
#define LV_USE_TABLE            1   /* Configuration screen pin table */
#define LV_USE_TABVIEW          1   /* Information screen: Live + System tabs */
#define LV_USE_TEXTAREA         0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/*================
   THEMES
 *================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   0
#define LV_THEME_DEFAULT_GROW   1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/*=========================
   FONT USAGE
 *=========================*/
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_36   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/*================
   OS / THREADING
 *================*/
#define LV_USE_OS               LV_OS_NONE   /* single-threaded event loop */

#endif /*LV_CONF_H*/
