#define _GNU_SOURCE
#include "config.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fallback addresses, derived once from nm'ing the gocontroll_demo ELF shipped
 * in this repo (same binary as in go-gtk-demo). These let the demo launch on
 * a fresh device even if parse_a2l.py has not been run against an .a2l yet. */
static const hmi_config_t k_defaults = {
    .k15                 = { .address = 0x423168, .type = DTYPE_UINT16, .size = 1 },
    .k30                 = { .address = 0x423180, .type = DTYPE_UINT16, .size = 1 },
    .display_brightness  = { .address = 0x42253d, .type = DTYPE_UINT8,  .size = 1 },
};

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (long)(16 * 1024 * 1024)) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

/* Parse { "name": { "address": int, "type": int, "size": int } } */
static int parse_entry(cJSON *root, const char *name, asap_element_t *out)
{
    cJSON *obj = cJSON_GetObjectItem(root, name);
    if (!cJSON_IsObject(obj)) return -1;
    cJSON *addr = cJSON_GetObjectItem(obj, "address");
    cJSON *type = cJSON_GetObjectItem(obj, "type");
    cJSON *size = cJSON_GetObjectItem(obj, "size");
    if (!cJSON_IsNumber(addr) || !cJSON_IsNumber(type)) return -1;
    out->address = (uintptr_t)addr->valuedouble;
    out->type    = (asap_datatype_t)type->valueint;
    out->size    = cJSON_IsNumber(size) ? (uint32_t)size->valueint : 1u;
    return 0;
}

int hmi_config_load(hmi_config_t *cfg, const char *json_dir)
{
    *cfg = k_defaults;

    char path[256];

    /* signals.json: k15, k30 */
    snprintf(path, sizeof(path), "%s/signals.json", json_dir);
    size_t n = 0;
    char *buf = read_file(path, &n);
    if (buf) {
        cJSON *root = cJSON_ParseWithLength(buf, n);
        if (root) {
            parse_entry(root, "k15", &cfg->k15);
            parse_entry(root, "k30", &cfg->k30);
            cJSON_Delete(root);
        }
        free(buf);
    }

    /* parameters.json: DisplayBrightness */
    snprintf(path, sizeof(path), "%s/parameters.json", json_dir);
    buf = read_file(path, &n);
    if (buf) {
        cJSON *root = cJSON_ParseWithLength(buf, n);
        if (root) {
            parse_entry(root, "DisplayBrightness", &cfg->display_brightness);
            cJSON_Delete(root);
        }
        free(buf);
    }

    return 0;
}
