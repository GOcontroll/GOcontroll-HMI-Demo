/* AUTO-GENERATED from GOcontroll-Architecture/modules/pinning.md.
 * Only the MDS1/MDS2 (Moduline HMI1) columns are kept. Regenerate via
 * scripts/gen_module_pins.py when the upstream doc changes. */
#include "module_pins.h"

/* 6 Channel Input Module  (article 201001) */
static const module_pin_t pins_00[] = {
    { "SUPPLY", "Sensor supply 5V", "A7", "A1" },
    { "SUPPLY", "Sensor supply 5V", "A6", "A2" },
    { "SUPPLY", "Sensor supply 5V", "A5", "A3" },
    { "IN1", "Input signal 1", "A15", "A10" },
    { "IN2", "Input signal 2", "A23", "A18" },
    { "IN3", "Input signal 3", "A14", "A11" },
    { "IN4", "Input signal 4", "A22", "A19" },
    { "IN5", "Input signal 5", "A13", "A12" },
    { "IN6", "Input signal 6", "A21", "A20" },
    { "GROUND", "Sensor ground", "A32", "A26" },
    { "GROUND", "Sensor ground", "A31", "A27" },
    { "GROUND", "Sensor ground", "A30", "A28" },
    { "NC", "No functionality", "A4", "A29" },
};

/* 6 Channel Output Module  (article 202002) */
static const module_pin_t pins_01[] = {
    { "POWER SUPPLY", "Power to the module", "A7", "A1" },
    { "POWER SUPPLY", "Power to the module", "A6", "A2" },
    { "POWER SUPPLY", "Power to the module", "A5", "A3" },
    { "OUT 1", "Actuator output channel 1", "A15", "A10" },
    { "OUT 2", "Actuator output channel 2", "A23", "A18" },
    { "OUT 3", "Actuator output channel 3", "A14", "A11" },
    { "OUT 4", "Actuator output channel 4", "A22", "A19" },
    { "OUT 5", "Actuator output channel 5", "A13", "A12" },
    { "OUT 6", "Actuator output channel 6", "A21", "A20" },
    { "POWER GROUND", "Ground to module", "A32", "A26" },
    { "POWER GROUND", "Ground to module", "A31", "A27" },
    { "POWER GROUND", "Ground to module", "A30", "A28" },
    { "NC", "No functionality", "A4", "A29" },
};

/* 10 Channel Input Module  (article 201002) */
static const module_pin_t pins_02[] = {
    { "SUPPLY", "Sensor supply 5V", "A6", "A1" },
    { "SUPPLY", "Sensor supply 5V", "A7", "A2" },
    { "IN1", "Input signal 1", "A15", "A10" },
    { "IN2", "Input signal 2", "A23", "A18" },
    { "IN3", "Input signal 3", "A14", "A11" },
    { "IN4", "Input signal 4", "A22", "A19" },
    { "IN5", "Input signal 5", "A13", "A12" },
    { "IN6", "Input signal 6", "A21", "A20" },
    { "IN7", "Input signal 7", "A31", "A27" },
    { "IN8", "Input signal 8", "A30", "A28" },
    { "IN9", "Input signal 9", "A5", "A3" },
    { "IN10", "Input signal 10", "A4", "A29" },
    { "GROUND", "Sensor ground", "A32", "A26" },
};

/* 10 Channel Output Module  (article 202003) */
static const module_pin_t pins_03[] = {
    { "POWER SUPPLY", "Power to the module", "A6", "A1" },
    { "POWER SUPPLY", "Power to the module", "A7", "A2" },
    { "OUT 1", "Actuator output channel 1", "A15", "A10" },
    { "OUT 2", "Actuator output channel 2", "A23", "A18" },
    { "OUT 3", "Actuator output channel 3", "A14", "A11" },
    { "OUT 4", "Actuator output channel 4", "A22", "A19" },
    { "OUT 5", "Actuator output channel 5", "A13", "A12" },
    { "OUT 6", "Actuator output channel 6", "A21", "A20" },
    { "OUT 7", "Actuator output channel 7", "A31", "A27" },
    { "OUT 8", "Actuator output channel 8", "A30", "A28" },
    { "OUT 9", "Actuator output channel 9", "A5", "A3" },
    { "OUT 10", "Actuator output channel 10", "A4", "A29" },
    { "POWER GROUND", "Ground to module", "A32", "A26" },
};

/* 4-20 mA Input Module  (article 201003) */
static const module_pin_t pins_04[] = {
    { "IN 1", "0 - 20 mA input 1 (sourcing ~16V)", "A7", "A1" },
    { "IN 2", "0 - 20 mA input 2 (sourcing ~16V)", "A15", "A10" },
    { "IN 3", "0 - 20 mA input 3 (sourcing ~16V)", "A23", "A18" },
    { "IN 4", "0 - 20 mA input 4 (sourcing ~16V)", "A6", "A2" },
    { "IN 5", "0 - 20 mA input 5 (sourcing ~16V)", "A14", "A11" },
    { "IN 6", "0 - 20 mA input 6 (sourcing ~16V)", "A22", "A19" },
    { "IN 7", "0 - 20 mA input 7 (sourcing ~16V)", "A5", "A3" },
    { "IN 8", "0 - 20 mA input 8 (sourcing ~16V)", "A13", "A12" },
    { "IN 9", "0 - 20 mA input 9 (sourcing ~16V)", "A21", "A20" },
    { "IN 10", "0 - 20 mA input 10 (sourcing ~16V)", "A4", "A29" },
    { "GROUND", "Current loop ground", "A32", "A26" },
    { "GROUND", "Current loop ground", "A31", "A27" },
    { "GROUND", "Current loop ground", "A30", "A28" },
};

/* 2 Channel Power Bridge Module  (article 202001) */
static const module_pin_t pins_05[] = {
    { "POWER SUPPLY", "Power to the module", "A7", "A1" },
    { "POWER SUPPLY", "Power to the module", "A6", "A2" },
    { "POWER SUPPLY", "Power to the module", "A5", "A3" },
    { "OUT 1", "Actuator output channel 1", "A15", "A10" },
    { "OUT 1", "Actuator output channel 1", "A14", "A11" },
    { "OUT 1", "Actuator output channel 1", "A13", "A12" },
    { "OUT 2", "Actuator output channel 2", "A23", "A18" },
    { "OUT 2", "Actuator output channel 2", "A22", "A19" },
    { "OUT 2", "Actuator output channel 2", "A21", "A20" },
    { "POWER GROUND", "Ground to module", "A32", "A26" },
    { "POWER GROUND", "Ground to module", "A31", "A27" },
    { "POWER GROUND", "Ground to module", "A30", "A28" },
    { "NC", "No functionality", "A4", "A29" },
};

/* Anleg IR Module  (article 203003) */
static const module_pin_t pins_06[] = {
    { "SUPPLY", "Supply to Module", "A7", "A1" },
    { "RS485/RS232TX", "RS485/RS232 TX output", "A6", "A2" },
    { "RS485/RS232RX", "RS485/RS232 RX input", "A5", "A3" },
    { "NC", "No functionality", "A15", "A10" },
    { "LED A", "LED out A", "A14", "A11" },
    { "IR IN", "IR emitter ring feedback", "A13", "A12" },
    { "NC", "No functionality", "A23", "A18" },
    { "LED B", "LED out B", "A22", "A19" },
    { "IR OUT", "IR emitter ring control out", "A21", "A20" },
    { "GROUND", "Ground to module", "A32", "A26" },
    { "CAN L", "CAN 1 Low", "A31", "A27" },
    { "CAN H", "CAN 1 High", "A30", "A28" },
    { "SUPPLY", "IR ring 5V or 12V supply", "A4", "A29" },
};

/* Multibus  (article 203004) */
static const module_pin_t pins_07[] = {
    { "SUPPLY", "Variable sensor supply 5 - 16V", "A7", "A1" },
    { "CAN H", "CAN 1 High", "A6", "A2" },
    { "CAN L", "CAN 1 Low", "A5", "A3" },
    { "RS485B/RS232OUT", "RS485 1 B or RS232 1 OUT (sw select)", "A15", "A10" },
    { "RS485A/RS232IN", "RS485 1 A or RS232 1 IN (sw select)", "A14", "A11" },
    { "ISOSPI IP", "IP pin form the ISOSPI interface", "A13", "A12" },
    { "RS485B/CAN H", "RS485 2 B or CAN 2 High (sw select)", "A23", "A18" },
    { "RS485A/CAN L", "RS485 2 A or CAN 2 Low (sw select)", "A22", "A19" },
    { "ISOSPI IM", "IM pin form the ISOSPI interface", "A21", "A20" },
    { "GROUND", "Ground to sensor/device", "A32", "A26" },
    { "RS485B/CAN H", "RS485 3 B or CAN 3 High (sw select)", "A31", "A27" },
    { "RS485A/CAN L", "RS485 3 A or CAN 3 Low (sw select)", "A30", "A28" },
    { "GROUND", "Ground to sensor/device", "A4", "A29" },
};

const module_def_t g_module_defs[] = {
    { "6 Channel Input Module", "201001", pins_00, 13 },
    { "6 Channel Output Module", "202002", pins_01, 13 },
    { "10 Channel Input Module", "201002", pins_02, 13 },
    { "10 Channel Output Module", "202003", pins_03, 13 },
    { "4-20 mA Input Module", "201003", pins_04, 13 },
    { "2 Channel Power Bridge Module", "202001", pins_05, 13 },
    { "Anleg IR Module", "203003", pins_06, 13 },
    { "Multibus", "203004", pins_07, 13 },
};

const size_t g_module_def_count = sizeof(g_module_defs) / sizeof(g_module_defs[0]);
