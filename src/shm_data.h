#ifndef GOCONTROLL_HMI_SHM_DATA_H
#define GOCONTROLL_HMI_SHM_DATA_H

#include <stdbool.h>

/* Process-data fields exposed via /dev/shm as plain ASCII float files.
 * The files are meant to be written/updated by an external producer
 * (Simulink model, test harness, etc.) and read at 1 Hz by the UI. */
typedef struct {
    float temperature;   /* deg C */
    float pressure;      /* bar   */
    float soc;           /* %     */
    float power;         /* kW    */
} shm_values_t;

/* Ensure /dev/shm/{TEMPERATURE,PRESSURE,SOC,POWER} exist; create each with
 * the default values 10/20/30/40 respectively if missing. Never overwrites
 * an existing file. Returns 0 on success, -1 if any write failed. */
int shm_data_seed_defaults(void);

/* Read current values. Fields that fail to parse are returned as NaN. */
void shm_data_read(shm_values_t *out);

/* True if v is a finite number (the caller uses this to decide whether to
 * show a dash). */
bool shm_data_valid(float v);

#endif
