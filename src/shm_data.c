#define _GNU_SOURCE
#include "shm_data.h"

#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const struct {
    const char *name;
    float       default_value;
    size_t      offset;
} k_fields[] = {
    { "TEMPERATURE", 10.0f, offsetof(shm_values_t, temperature) },
    { "PRESSURE",    20.0f, offsetof(shm_values_t, pressure)    },
    { "SOC",         30.0f, offsetof(shm_values_t, soc)         },
    { "POWER",       40.0f, offsetof(shm_values_t, power)       },
};
#define K_FIELD_COUNT (sizeof(k_fields) / sizeof(k_fields[0]))

static int write_default(const char *path, float value)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%.1f\n", value);
    if (len <= 0) return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return -1;
    ssize_t n = write(fd, buf, (size_t)len);
    close(fd);
    return (n == len) ? 0 : -1;
}

int shm_data_seed_defaults(void)
{
    int bad = 0;
    for (size_t i = 0; i < K_FIELD_COUNT; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/shm/%s", k_fields[i].name);
        /* Skip if the file already exists — don't clobber live data. */
        if (access(path, F_OK) == 0) continue;
        if (write_default(path, k_fields[i].default_value) != 0) bad++;
    }
    return bad ? -1 : 0;
}

static float read_one(const char *name)
{
    char path[64];
    snprintf(path, sizeof(path), "/dev/shm/%s", name);
    FILE *f = fopen(path, "r");
    if (!f) return NAN;
    float v = NAN;
    if (fscanf(f, "%f", &v) != 1) v = NAN;
    fclose(f);
    return v;
}

void shm_data_read(shm_values_t *out)
{
    for (size_t i = 0; i < K_FIELD_COUNT; i++) {
        float v = read_one(k_fields[i].name);
        /* Store into the struct field by offset. */
        *(float *)((char *)out + k_fields[i].offset) = v;
    }
}

bool shm_data_valid(float v)
{
    return isfinite(v) != 0;
}
