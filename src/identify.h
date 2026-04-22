#ifndef GOCONTROLL_HMI_IDENTIFY_H
#define GOCONTROLL_HMI_IDENTIFY_H

#define IDENTIFY_STR_MAX   128
#define IDENTIFY_MAX_SLOTS 8

/* One-shot snapshot of the controller's identity — the same fields the
 * /usr/bin/identify script prints, read straight from /etc/os-release,
 * /sys/firmware/devicetree, /etc/go-sn and /usr/lib/gocontroll/modules so
 * we never fork. All strings are nul-terminated; absent fields are "". */
typedef struct {
    char type[64];          /* human-readable module type or "-" */
    char hw_version[16];
    char sw_version[16];
    char manufacturer[32];
    char qr_front[32];
    char qr_back[32];
} identify_module_t;

typedef struct {
    char os[IDENTIFY_STR_MAX];
    char kernel[IDENTIFY_STR_MAX];
    char model[IDENTIFY_STR_MAX];
    char hardware[IDENTIFY_STR_MAX];
    char serial[32];

    int               module_count;
    identify_module_t modules[IDENTIFY_MAX_SLOTS];
} identify_t;

void identify_collect(identify_t *out);

#endif
