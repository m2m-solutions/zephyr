/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/sensor.h>
#include <init.h>
#include <logging/log.h>
#include <kernel.h>
#include <stdint.h>

typedef struct {
    const struct device *i2c;  /* I2C controller device */
    uint16_t i2c_addr;         /* 7-bit address (0x29) */
} VL53L4CD_Platform_t;

typedef VL53L4CD_Platform_t* Dev_t;

/* Required by ULD (signatures must match!) */
int VL53L4CD_WriteMulti (VL53L4CD_Platform_t *pdev, uint16_t reg, const uint8_t *data, uint32_t count);
int VL53L4CD_ReadMulti  (VL53L4CD_Platform_t *pdev, uint16_t reg, uint8_t *data, uint32_t count);
int VL53L4CD_WrByte     (VL53L4CD_Platform_t *pdev, uint16_t reg, uint8_t value);
int VL53L4CD_RdByte     (VL53L4CD_Platform_t *pdev, uint16_t reg, uint8_t *value);
int VL53L4CD_WrWord     (VL53L4CD_Platform_t *pdev, uint16_t reg, uint16_t value);
int VL53L4CD_RdWord     (VL53L4CD_Platform_t *pdev, uint16_t reg, uint16_t *value);
int VL53L4CD_WrDWord    (VL53L4CD_Platform_t *pdev, uint16_t reg, uint32_t value);
int VL53L4CD_RdDWord    (VL53L4CD_Platform_t *pdev, uint16_t reg, uint32_t *value);
int VL53L4CD_WaitMs     (VL53L4CD_Platform_t *pdev, uint32_t time_ms);