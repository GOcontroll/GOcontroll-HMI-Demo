/* AUTO-GENERATED from GOcontroll-Architecture/controller/static/pinning.md
 * section "Moduline HMI1 / HMI2 - Connector A - Vaste systeem-pins".
 * Only the power (K15/K30/K31) and CAN rows are kept; module-slot pins
 * live in src/module_pins.c. Regenerate via scripts/gen_controller_pins.py
 * when the upstream doc changes. */
#include "controller_pins.h"

const controller_pin_t g_hmi1_system_pins[] = {
    { "A8",  "K30",       "Supply"              },
    { "A9",  "K15",       "Contact (active high)" },
    { "A16", "CAN 1 LOW", "CAN low"             },
    { "A17", "CAN 1 HI",  "CAN high"            },
    { "A24", "CAN 2 LOW", "CAN low"             },
    { "A25", "CAN 2 HI",  "CAN high"            },
    { "A33", "K31",       "Ground"              },
    { "A34", "K31",       "Ground"              },
};

const size_t g_hmi1_system_pin_count =
    sizeof(g_hmi1_system_pins) / sizeof(g_hmi1_system_pins[0]);
