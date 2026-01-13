/*
 * Copyright (c) 2025 M2M-Solutions AB
 * SPDX-License-Identifier: Apache-2.0
 */

#include "vl53l4cd_platform.h"
#include <string.h>

LOG_MODULE_REGISTER(vl53l4cd_platform, LOG_LEVEL_INF);

/* In Zephyr 2.6 this is available and does a repeated-start write-then-read */
static inline int i2c_reg16_read(const struct device *i2c, uint16_t addr,
                                 uint16_t reg, uint8_t *data, uint32_t len)
{
    uint8_t regbuf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_write_read(i2c, addr, regbuf, sizeof(regbuf), data, len);
}

static inline int i2c_reg16_write(const struct device *i2c, uint16_t addr,
                                  uint16_t reg, const uint8_t *data, uint32_t len)
{
    /* ULD writes are small. Support up to 64 bytes in one go. */
    if (len > 64U) {
        return -ENOMEM; /* or split if you really need larger */
    }
    uint8_t buf[2 + 64];
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(&buf[2], data, len);
    return i2c_write(i2c, buf, 2 + len, addr);
}

/* ---- Platform API expected by ST's ULD ---- */

int VL53L4CD_WriteMulti(VL53L4CD_Platform_t *pdev, uint16_t reg,
                        const uint8_t *data, uint32_t count)
{
    if (!pdev || !pdev->i2c) {
        return -EINVAL;
    }
    int ret = i2c_reg16_write(pdev->i2c, pdev->i2c_addr, reg, data, count);
    return ret == 0 ? 0 : -EIO;
}

int VL53L4CD_ReadMulti(VL53L4CD_Platform_t *pdev, uint16_t reg,
                       uint8_t *data, uint32_t count)
{
    if (!pdev || !pdev->i2c) {
        return -EINVAL;
    }
    int ret = i2c_reg16_read(pdev->i2c, pdev->i2c_addr, reg, data, count);
    return ret == 0 ? 0 : -EIO;
}

int VL53L4CD_WrByte(VL53L4CD_Platform_t *pdev, uint16_t reg, uint8_t value)
{
    return VL53L4CD_WriteMulti(pdev, reg, &value, 1);
}

int VL53L4CD_RdByte(VL53L4CD_Platform_t *pdev, uint16_t reg, uint8_t *value)
{
    return VL53L4CD_ReadMulti(pdev, reg, value, 1);
}

int VL53L4CD_WaitMs(VL53L4CD_Platform_t *pdev, uint32_t time_ms)
{
    ARG_UNUSED(pdev);
    k_msleep(time_ms);
    return 0;
}

int VL53L4CD_WrWord(VL53L4CD_Platform_t *pdev, uint16_t reg, uint16_t value)
{
    uint8_t buf[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return VL53L4CD_WriteMulti(pdev, reg, buf, 2);
}

int VL53L4CD_RdWord(VL53L4CD_Platform_t *pdev, uint16_t reg, uint16_t *value)
{
    uint8_t buf[2];
    int r = VL53L4CD_ReadMulti(pdev, reg, buf, 2);
    if (r) return r;
    *value = ((uint16_t)buf[0] << 8) | buf[1];
    return 0;
}

int VL53L4CD_WrDWord(VL53L4CD_Platform_t *pdev, uint16_t reg, uint32_t value)
{
    uint8_t buf[4] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),  (uint8_t)(value)
    };
    return VL53L4CD_WriteMulti(pdev, reg, buf, 4);
}

int VL53L4CD_RdDWord(VL53L4CD_Platform_t *pdev, uint16_t reg, uint32_t *value)
{
    uint8_t buf[4];
    int r = VL53L4CD_ReadMulti(pdev, reg, buf, 4);
    if (r) return r;
    *value = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
           | ((uint32_t)buf[2] << 8)  | ((uint32_t)buf[3]);
    return 0;
}