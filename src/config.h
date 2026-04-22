#ifndef GOCONTROLL_HMI_CONFIG_H
#define GOCONTROLL_HMI_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* Subset of asap_datatypes enum values used by the Moduline python binding.
 * Kept in sync with /usr/lib/python3/dist-packages/pyuio/pyuiobind so that the
 * same signals.json/parameters.json works across implementations. */
typedef enum {
    DTYPE_UINT8    = 0,
    DTYPE_INT8     = 1,
    DTYPE_UINT16   = 2,
    DTYPE_INT16    = 3,
    DTYPE_UINT32   = 4,
    DTYPE_INT32    = 5,
    DTYPE_UINT64   = 6,
    DTYPE_INT64    = 7,
    DTYPE_SINGLE   = 8,
    DTYPE_DOUBLE   = 9,
    DTYPE_BOOLEAN  = 10,
} asap_datatype_t;

typedef struct {
    uintptr_t       address;
    asap_datatype_t type;
    uint32_t        size;       /* number of elements; 1 for scalars */
} asap_element_t;

typedef struct {
    asap_element_t k15;
    asap_element_t k30;
    asap_element_t display_brightness;
} hmi_config_t;

/**
 * Populate cfg with addresses/types. Reads from json_dir (typically
 * /usr/share/gocontroll-hmi-demo). Falls back to built-in defaults derived
 * from nm'ing the supplied gocontroll_demo ELF if the files are absent.
 *
 * @return 0 on success, -1 on hard failure (address of 0 in all fields).
 */
int hmi_config_load(hmi_config_t *cfg, const char *json_dir);

/** Width of the value returned by a given asap_datatype_t, in bytes. */
static inline size_t asap_datatype_size(asap_datatype_t t)
{
    switch (t) {
    case DTYPE_UINT8:
    case DTYPE_INT8:
    case DTYPE_BOOLEAN:  return 1;
    case DTYPE_UINT16:
    case DTYPE_INT16:    return 2;
    case DTYPE_UINT32:
    case DTYPE_INT32:
    case DTYPE_SINGLE:   return 4;
    case DTYPE_UINT64:
    case DTYPE_INT64:
    case DTYPE_DOUBLE:   return 8;
    }
    return 1;
}

#endif
