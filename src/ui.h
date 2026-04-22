#ifndef GOCONTROLL_HMI_UI_H
#define GOCONTROLL_HMI_UI_H

#include <sys/types.h>
#include "config.h"

/**
 * Construct the demo UI on the active LVGL display.
 *
 * @param demo_pid PID of the gocontroll_demo Simulink process (for pyuio-like
 *                 IPC). If < 0, UI is built without live IPC and voltages show
 *                 as "-mV".
 * @param cfg      resolved addresses for k15, k30, DisplayBrightness.
 * @param asset_dir directory containing GOcontroll.png (e.g.
 *                 /usr/share/gocontroll-hmi-demo).
 */
void ui_build(pid_t demo_pid, const hmi_config_t *cfg, const char *asset_dir);

/** Shut down the UI-owned timer and IPC state. */
void ui_shutdown(void);

/** Returns 1 once the user has pressed "Exit demo". */
int ui_should_exit(void);

#endif
