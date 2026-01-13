/*
 * Zephyr driver glue for ST VL53L4CD ULD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <device.h>
#include <drivers/sensor.h>
#include <devicetree.h>
#include <sys/byteorder.h>
#include <logging/log.h>
#include <drivers/gpio.h>

#include "vl53l4cd_platform.h"
#include "VL53L4CD_api.h"  /* from ST ULD package you’ve added to modules */

#ifdef __cplusplus
extern "C" {
#endif

/* DT compatible must match your binding */
#define DT_DRV_COMPAT st_vl53l4cd

struct vl53l4cd_config {
    const char *i2c_bus_label;
    uint16_t i2c_addr;
    uint16_t timing_budget_ms;
    uint16_t inter_measure_ms;   
    struct gpio_dt_spec xshut;
    bool xshut_present;
};

struct vl53l4cd_data {
    const struct device *i2c;
    VL53L4CD_Platform_t plat;

    /* cached measurement */
    int16_t distance_mm;
    uint8_t range_status;  /* 0 OK, others per ULD doc */
    bool data_valid;

    uint8_t timing_budget_ms;
    uint8_t inter_measure_ms;
};

int vl53l4cd_reinit(const struct device *dev);
int vl53l4cd_set_timing(const struct device *dev, uint8_t timing_budget_ms, uint8_t inter_measure_ms);

#ifdef __cplusplus
}
#endif