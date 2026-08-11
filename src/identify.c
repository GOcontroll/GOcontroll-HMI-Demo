#define _GNU_SOURCE
#include "identify.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

/* Same mapping as /usr/bin/identify's get_module_name(). */
static const char *module_name(const char *id)
{
    if (strstr(id, "20-10-1")) return "6 channel input";
    if (strstr(id, "20-10-2")) return "10 channel input";
    if (strstr(id, "20-10-3")) return "4-20 mA input";
    if (strstr(id, "20-20-1")) return "2 channel power bridge";
    if (strstr(id, "20-20-2")) return "6 channel output";
    if (strstr(id, "20-30-3")) return "IR communication";
    return NULL;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\r')) s[--n] = 0;
}

/* The serial number is not a file. /usr/bin/go-sn keeps it as a bare 19-byte
 * ASCII string ("XXXX-XXXX-XXXX-XXXX", four alphanumeric groups joined by '-')
 * at the start of the GPT partition labelled 'serial', and falls back to the
 * last sector of /dev/mmcblk0 on legacy units flashed before that partition
 * existed. Read both the same way so we never fork; we run as root, so the raw
 * block device is readable. An unprogrammed unit leaves the area zeroed or
 * erased, which fails validation and comes back as "" — the UI shows "-". */
#define SERIAL_LEN 19
#define SERIAL_LEGACY_SECTOR 512

static int serial_is_valid(const char *s)
{
    for (int i = 0; i < SERIAL_LEN; i++) {
        if (i % 5 == 4) { if (s[i] != '-')                       return 0; }
        else            { if (!isalnum((unsigned char)s[i]))      return 0; }
    }
    return s[SERIAL_LEN] == 0;
}

static int read_serial_from(const char *path, int last_sector, char *out, size_t out_sz)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (last_sector && fseek(f, -SERIAL_LEGACY_SECTOR, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    char buf[SERIAL_LEN + 1] = {0};
    size_t got = fread(buf, 1, SERIAL_LEN, f);
    fclose(f);
    if (got != SERIAL_LEN || !serial_is_valid(buf)) return -1;
    snprintf(out, out_sz, "%s", buf);
    return 0;
}

static void read_serial(char *out, size_t out_sz)
{
    out[0] = 0;
    if (read_serial_from("/dev/disk/by-partlabel/serial", 0, out, out_sz) == 0) return;
    if (read_serial_from("/dev/mmcblk0",                  1, out, out_sz) == 0) return;
    out[0] = 0;
}

static void read_os_pretty(char *out, size_t out_sz)
{
    out[0] = 0;
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PRETTY_NAME=", 12) != 0) continue;
        const char *v = line + 12;
        if (*v == '"') v++;
        snprintf(out, out_sz, "%s", v);
        size_t n = strlen(out);
        while (n && (out[n-1] == '\n' || out[n-1] == '"')) out[--n] = 0;
        break;
    }
    fclose(f);
}

/* /sys/firmware/devicetree/base/* entries are NUL-terminated binary strings —
 * fgets wouldn't find a newline. Read the whole file, then trim trailing NULs
 * and whitespace. */
static void read_dt_string(const char *path, char *out, size_t out_sz)
{
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t got = fread(out, 1, out_sz - 1, f);
    out[got] = 0;
    fclose(f);
    while (got && (out[got-1] == 0 || isspace((unsigned char)out[got-1]))) out[--got] = 0;
}

static void read_modules(identify_t *out)
{
    out->module_count = 0;
    FILE *f = fopen("/usr/lib/gocontroll/modules", "r");
    if (!f) return;

    char layout[512] = {0};
    char manuf [512] = {0};
    char qrf   [512] = {0};
    char qrb   [512] = {0};
    if (!fgets(layout, sizeof(layout), f)) { fclose(f); return; }
    fgets(manuf, sizeof(manuf), f);
    fgets(qrf,   sizeof(qrf),   f);
    fgets(qrb,   sizeof(qrb),   f);
    fclose(f);

    trim(layout); trim(manuf); trim(qrf); trim(qrb);

    /* The file uses ':' as separator; an empty slot is just an empty field. */
    char *save[4] = {NULL, NULL, NULL, NULL};
    char *tok_layout = strtok_r(layout, ":", &save[0]);
    char *tok_manuf  = strtok_r(manuf,  ":", &save[1]);
    char *tok_qrf    = strtok_r(qrf,    ":", &save[2]);
    char *tok_qrb    = strtok_r(qrb,    ":", &save[3]);

    while (tok_layout && out->module_count < IDENTIFY_MAX_SLOTS) {
        identify_module_t *m = &out->modules[out->module_count++];
        const char *name = module_name(tok_layout);
        if (!name) {
            snprintf(m->type,         sizeof(m->type),         "-");
            snprintf(m->hw_version,   sizeof(m->hw_version),   "-");
            snprintf(m->sw_version,   sizeof(m->sw_version),   "-");
            snprintf(m->manufacturer, sizeof(m->manufacturer), "-");
            snprintf(m->qr_front,     sizeof(m->qr_front),     "-");
            snprintf(m->qr_back,      sizeof(m->qr_back),      "-");
        } else {
            snprintf(m->type, sizeof(m->type), "%s", name);
            /* layout token looks like "20-10-1-02-01-00-05": last four dashes
             * split into HW version + three SW version numbers. */
            char parts[8][16] = {{0}};
            int np = 0;
            char copy[64]; snprintf(copy, sizeof(copy), "%s", tok_layout);
            char *sp = NULL;
            for (char *t = strtok_r(copy, "-", &sp); t && np < 8; t = strtok_r(NULL, "-", &sp))
                snprintf(parts[np++], sizeof(parts[0]), "%s", t);
            snprintf(m->hw_version, sizeof(m->hw_version), "%s", np > 3 ? parts[3] : "-");
            if (np > 6) snprintf(m->sw_version, sizeof(m->sw_version), "%s.%s.%s",
                                 parts[4], parts[5], parts[6]);
            else        snprintf(m->sw_version, sizeof(m->sw_version), "-");
            snprintf(m->manufacturer, sizeof(m->manufacturer), "%s", tok_manuf ? tok_manuf : "-");
            snprintf(m->qr_front,     sizeof(m->qr_front),     "%s", tok_qrf   ? tok_qrf   : "-");
            snprintf(m->qr_back,      sizeof(m->qr_back),      "%s", tok_qrb   ? tok_qrb   : "-");
        }
        tok_layout = strtok_r(NULL, ":", &save[0]);
        tok_manuf  = tok_manuf ? strtok_r(NULL, ":", &save[1]) : NULL;
        tok_qrf    = tok_qrf   ? strtok_r(NULL, ":", &save[2]) : NULL;
        tok_qrb    = tok_qrb   ? strtok_r(NULL, ":", &save[3]) : NULL;
    }
}

void identify_collect(identify_t *out)
{
    memset(out, 0, sizeof(*out));

    read_os_pretty(out->os, sizeof(out->os));

    struct utsname u;
    if (uname(&u) == 0) snprintf(out->kernel, sizeof(out->kernel), "%s", u.release);

    /* Captured verbatim. The device tree still carries the pre-rename product
     * name: on an HMI1, model reads "GOcontroll Moduline Display with BOE
     * av101hdt-a10 display" and hardware reads "Moduline Display V1.xx". Do
     * not rewrite it here — this is the board's own identity as reported by
     * the kernel, and it is the same string /usr/bin/gocontroll_demo matches
     * on to detect the board. The Information screen swaps the product name
     * for display only; see product_name_for_display() in ui.c. */
    read_dt_string("/sys/firmware/devicetree/base/model",    out->model,    sizeof(out->model));
    read_dt_string("/sys/firmware/devicetree/base/hardware", out->hardware, sizeof(out->hardware));

    read_serial(out->serial, sizeof(out->serial));

    read_modules(out);
}
