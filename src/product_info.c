/* Product-info snapshot for the Information screen. Manually curated from
 * https://gocontroll.com/gocontroll-moduline-hmi1/ — update alongside
 * any product-page revision. */
#include "product_info.h"

const char *const g_product_tagline =
    "Modular HMI embedded controller with graphics focus";

const char *const g_product_description =
    "Linux-based Human-Machine Interface controller for automotive and "
    "industrial applications. Supports MATLAB/Simulink and Node-RED visual "
    "programming flows. IP67 enclosure, wide supply range.";

static const product_info_row_t sec_compute[] = {
    { "Processor", "NXP i.MX 8M Plus"     },
    { "Cores",     "4x ARM Cortex-A53"    },
    { "Clock",     "1.6 GHz"              },
    { "RAM",       "2 GB"                 },
    { "Flash",     "8 GB eMMC"            },
    { "Boot",      "3 - 3.5 s"            },
};

static const product_info_row_t sec_display[] = {
    { "Outputs",   "HDMI 2.0a, LVDS, MIPI-DSI" },
    { "Camera",    "1x MIPI-CSI"               },
    { "Audio",     "1x SAI"                    },
};

static const product_info_row_t sec_connectivity[] = {
    { "Ethernet",  "10/100 Mbps"         },
    { "CAN",       "2 channels"          },
    { "Bluetooth", "5.1 BR/EDR/LE (opt.)" },
    { "Wi-Fi",     "802.11 b/g/n (opt.)" },
    { "Slots",     "2x module (MDS1/MDS2)" },
};

static const product_info_row_t sec_power[] = {
    { "Supply",    "8 - 32 VDC"          },
    { "At 12 V",   "310 mA nominal"      },
    { "At 24 V",   "200 mA nominal"      },
    { "Standby",   "< 1 mA"              },
    { "Protection", "ISO 7637-2 5a/5b"   },
};

static const product_info_row_t sec_env[] = {
    { "Temperature", "-20 to +85 deg C"  },
    { "IP rating",   "IP67"              },
    { "Gasket",      "Neoprene + PES breather" },
};

#define SEC(heading, rows) \
    { heading, rows, sizeof(rows) / sizeof(rows[0]) }

const product_info_section_t g_product_sections[] = {
    SEC("Compute",      sec_compute),
    SEC("Display I/O",  sec_display),
    SEC("Connectivity", sec_connectivity),
    SEC("Power",        sec_power),
    SEC("Environment",  sec_env),
};

const size_t g_product_section_count =
    sizeof(g_product_sections) / sizeof(g_product_sections[0]);
