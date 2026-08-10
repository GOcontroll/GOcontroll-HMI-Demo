#ifndef GOCONTROLL_HMI_UI_H
#define GOCONTROLL_HMI_UI_H

#include <sys/types.h>
#include "config.h"
#include "settings.h"

/**
 * Build the three-screen dashboard (main + information + settings) on the
 * active LVGL display. Loads the main screen once construction is done.
 *
 * @param demo_pid PID of the IPC_PROCESS_NAME Simulink process for K15/K30 and
 *                 brightness IPC, or < 0 if it is not running yet. Only a
 *                 starting point: the UI re-resolves the pid whenever a
 *                 transfer fails, so a backend that starts or restarts later is
 *                 picked up within a couple of seconds. While there is none,
 *                 the affected values show "-".
 * @param cfg      resolved asap addresses.
 * @param asset_dir directory containing GOcontroll.png /
 *                 GOcontroll_header.png (/usr/share/gocontroll-hmi-demo).
 * @param settings persisted preferences; mutated by the Settings screen and
 *                 re-saved on change.
 */
void ui_build(pid_t demo_pid, const hmi_config_t *cfg, const char *asset_dir,
              hmi_settings_t *settings);

/** Shut down UI-owned timers. */
void ui_shutdown(void);

/** Returns 1 once the user has pressed "Exit demo" on the Settings screen. */
int ui_should_exit(void);

#endif
