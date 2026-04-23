#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdbool.h>
#include <stddef.h>

/* Primary non-loopback IPv4 address, e.g. "192.168.1.42". Empty string if no
 * non-loopback interface has an IPv4 yet (just booted, no cable, …). Buffer
 * must be >= 16 bytes (INET_ADDRSTRLEN). */
void sysinfo_ipv4(char *out, size_t out_sz);

/* MAC address of the same interface that sysinfo_ipv4 picked (or first
 * operational non-loopback if none has an IPv4 yet). Formatted as
 * "aa:bb:cc:dd:ee:ff". Empty on failure. Buffer must be >= 18 bytes. */
void sysinfo_mac(char *out, size_t out_sz);

/* CPU core temperature in °C. Returns -1000.0 if the thermal zone can't be
 * read. First call scans /sys/class/thermal for type "cpu-thermal" and caches
 * the path. */
float sysinfo_cpu_temp_c(void);

#endif
