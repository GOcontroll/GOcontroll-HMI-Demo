#ifndef GOCONTROLL_HMI_PRODUCT_INFO_H
#define GOCONTROLL_HMI_PRODUCT_INFO_H

#include <stddef.h>

/* Static snapshot of the general product information for the Moduline
 * Display I (HMI1), sourced from
 *   https://gocontroll.com/gocontroll-moduline-display-i/
 * at package build time. Kept intentionally terse so it fits on the right
 * half of the Information screen. Connector / pin details are explicitly
 * excluded — those live in controller_pins.c and module_pins.c. */
typedef struct {
    const char *key;
    const char *value;
} product_info_row_t;

typedef struct {
    const char              *heading;
    const product_info_row_t *rows;
    size_t                   row_count;
} product_info_section_t;

/* Short tagline shown above the sectioned info. */
extern const char *const g_product_tagline;

/* Longer single-paragraph description. */
extern const char *const g_product_description;

extern const product_info_section_t g_product_sections[];
extern const size_t                 g_product_section_count;

#endif
