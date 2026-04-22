#define _GNU_SOURCE
#include "settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <cjson/cJSON.h>

#define SETTINGS_DIR  "/var/lib/gocontroll-hmi-demo"
#define SETTINGS_PATH SETTINGS_DIR "/settings.json"

void hmi_settings_load(hmi_settings_t *s)
{
    /* Defaults — used if the file is missing or malformed. */
    s->show_clock = true;

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 65536) { fclose(f); return; }

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = 0;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;

    cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "show_clock");
    if (cJSON_IsBool(sc)) s->show_clock = cJSON_IsTrue(sc);

    cJSON_Delete(root);
}

void hmi_settings_save(const hmi_settings_t *s)
{
    if (mkdir(SETTINGS_DIR, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "settings: mkdir %s: %s\n", SETTINGS_DIR, strerror(errno));
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "show_clock", s->show_clock);
    char *txt = cJSON_Print(root);
    cJSON_Delete(root);
    if (!txt) return;

    /* Write-then-rename so a crash mid-write can't corrupt the file. */
    const char *tmp_path = SETTINGS_PATH ".tmp";
    FILE *f = fopen(tmp_path, "w");
    if (!f) { fprintf(stderr, "settings: open %s: %s\n", tmp_path, strerror(errno)); free(txt); return; }
    fputs(txt, f);
    fclose(f);
    free(txt);

    if (rename(tmp_path, SETTINGS_PATH) != 0) {
        fprintf(stderr, "settings: rename: %s\n", strerror(errno));
    }
}
