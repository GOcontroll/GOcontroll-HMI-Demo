#ifndef GOCONTROLL_HMI_MODULE_PINS_H
#define GOCONTROLL_HMI_MODULE_PINS_H

#include <stddef.h>

/* Pin table for one module type on the Moduline HMI1 controller only.
 * Columns: function, description, MDS1 pin, MDS2 pin. All strings are
 * static string literals so no allocation or cleanup is needed. */
typedef struct {
    const char *function;
    const char *description;
    const char *mds1;
    const char *mds2;
} module_pin_t;

typedef struct {
    const char        *name;
    const char        *article_number;
    const module_pin_t *pins;
    size_t             pin_count;
} module_def_t;

extern const module_def_t g_module_defs[];
extern const size_t       g_module_def_count;

#endif
