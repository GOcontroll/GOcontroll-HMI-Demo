#define _GNU_SOURCE
#include "sysinfo.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* Pick the same interface sysinfo_ipv4 would return: first non-loopback,
 * IFF_UP, with an IPv4 assigned. Fallback to the first non-loopback/IFF_UP
 * interface if nothing has IPv4 yet, so the Information screen still shows
 * the MAC of a cable that has been plugged in but hasn't DHCP'd. Caller
 * owns iface_out[IFNAMSIZ]. */
static int pick_primary_iface(char *iface_out, size_t iface_sz)
{
    if (iface_sz == 0) return -1;
    iface_out[0] = 0;

    struct ifaddrs *head = NULL;
    if (getifaddrs(&head) != 0) return -1;

    char fallback[IFNAMSIZ] = {0};
    for (struct ifaddrs *it = head; it; it = it->ifa_next) {
        if (!it->ifa_addr || !it->ifa_name) continue;
        if (it->ifa_flags & IFF_LOOPBACK) continue;
        if (!(it->ifa_flags & IFF_UP)) continue;

        if (it->ifa_addr->sa_family == AF_INET) {
            snprintf(iface_out, iface_sz, "%s", it->ifa_name);
            freeifaddrs(head);
            return 0;
        }
        if (!fallback[0])
            snprintf(fallback, sizeof(fallback), "%s", it->ifa_name);
    }
    freeifaddrs(head);

    if (fallback[0]) {
        snprintf(iface_out, iface_sz, "%s", fallback);
        return 0;
    }
    return -1;
}

void sysinfo_ipv4(char *out, size_t out_sz)
{
    if (out_sz == 0) return;
    out[0] = 0;

    struct ifaddrs *head = NULL;
    if (getifaddrs(&head) != 0) return;

    for (struct ifaddrs *it = head; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if (it->ifa_flags & IFF_LOOPBACK) continue;
        if (!(it->ifa_flags & IFF_UP)) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)it->ifa_addr;
        inet_ntop(AF_INET, &sin->sin_addr, out, (socklen_t)out_sz);
        break;
    }
    freeifaddrs(head);
}

void sysinfo_mac(char *out, size_t out_sz)
{
    if (out_sz == 0) return;
    out[0] = 0;
    if (out_sz < 18) return;

    char iface[IFNAMSIZ];
    if (pick_primary_iface(iface, sizeof(iface)) != 0) return;

    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)out_sz, f)) {
        size_t n = strlen(out);
        while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = 0;
    }
    fclose(f);
}

static const char *cpu_thermal_path(void)
{
    /* Cache the matching /sys/class/thermal/thermal_zoneN/temp path. The zone
     * index is not fixed across boots — we match by the "type" attribute. */
    static char cached[320];
    static int resolved = 0;
    if (resolved) return cached[0] ? cached : NULL;
    resolved = 1;

    DIR *d = opendir("/sys/class/thermal");
    if (!d) return NULL;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "thermal_zone", 12) != 0) continue;

        char tpath[320];
        snprintf(tpath, sizeof(tpath), "/sys/class/thermal/%s/type", de->d_name);
        FILE *f = fopen(tpath, "r");
        if (!f) continue;
        char type[64] = {0};
        if (fgets(type, sizeof(type), f)) {
            size_t n = strlen(type);
            if (n && type[n - 1] == '\n') type[n - 1] = 0;
        }
        fclose(f);

        if (strcmp(type, "cpu-thermal") == 0) {
            snprintf(cached, sizeof(cached), "/sys/class/thermal/%s/temp", de->d_name);
            break;
        }
    }
    closedir(d);
    return cached[0] ? cached : NULL;
}

float sysinfo_cpu_temp_c(void)
{
    const char *path = cpu_thermal_path();
    if (!path) return -1000.0f;

    FILE *f = fopen(path, "r");
    if (!f) return -1000.0f;
    int millideg = 0;
    int ok = fscanf(f, "%d", &millideg) == 1;
    fclose(f);
    return ok ? (millideg / 1000.0f) : -1000.0f;
}
