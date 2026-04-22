#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

/* Persistent UI preferences, stored at /var/lib/gocontroll-hmi-demo/settings.json.
 * Missing file or fields fall back to defaults; save failures are logged but
 * non-fatal so the UI keeps working on a read-only filesystem. */
typedef struct {
    bool show_clock;  /* default true */
} hmi_settings_t;

void hmi_settings_load(hmi_settings_t *s);
void hmi_settings_save(const hmi_settings_t *s);

#endif
