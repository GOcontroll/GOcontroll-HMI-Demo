#ifndef GOCONTROLL_HMI_CONTROLLER_PINS_H
#define GOCONTROLL_HMI_CONTROLLER_PINS_H

#include <stddef.h>

/* HMI1 controller system-pin table (connector A, fixed pins only).
 * Covers the K15 / K30 / K31 power pins and the two CAN buses — the subset
 * that the demo's Settings screen displays next to the back-of-enclosure
 * image. Auto-generated from GOcontroll-Architecture/controller/static/pinning.md;
 * regenerate with scripts/gen_controller_pins.py when upstream changes. */
typedef struct {
    const char *pin;          /* e.g. "A8" */
    const char *signal;       /* e.g. "K30" */
    const char *description;  /* e.g. "Supply" */
} controller_pin_t;

extern const controller_pin_t g_hmi1_system_pins[];
extern const size_t           g_hmi1_system_pin_count;

#endif
