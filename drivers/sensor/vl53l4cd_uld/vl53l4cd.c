/*
 * Zephyr driver glue for ST VL53L4CD ULD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "vl53l4cd.h"
#include <drivers/gpio.h>

LOG_MODULE_REGISTER(vl53l4cd, CONFIG_SENSOR_LOG_LEVEL);

static int vl53l4cd_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    struct vl53l4cd_data *data = dev->data;
    int status;
    uint8_t is_ready = 0;
    uint32_t loops = 0;

    ARG_UNUSED(chan);

    /* Poll for data ready with a bounded wait (roughly timing budget + a guard) */
    do {
        status = VL53L4CD_CheckForDataReady(&data->plat, &is_ready);
        if (status != 0) {
            LOG_ERR("CheckForDataReady failed: %d", status);
            return -EIO;
        }

        if (!is_ready) {
            k_msleep(2);
        }
    } while (!is_ready && (++loops < 1000)); /* ~2 seconds max */

    if (!is_ready) {
        LOG_WRN("Data not ready (timeout)");
        data->data_valid = false;
        return -EAGAIN;
    }

    /* Read result */
    VL53L4CD_ResultsData_t res;
    status = VL53L4CD_GetResult(&data->plat, &res);
    if (status != 0) {
        LOG_ERR("GetResult failed: %d", status);
        data->data_valid = false;
        return -EIO;
    }

    /* Clear interrupt / ready flag per ULD flow */
    (void)VL53L4CD_ClearInterrupt(&data->plat);

    data->distance_mm = (int16_t)res.distance_mm;
    data->range_status = res.range_status;
    data->data_valid = (res.range_status == 0);

    return 0;
}

static int vl53l4cd_channel_get(const struct device *dev,
                                enum sensor_channel chan,
                                struct sensor_value *val)
{
    const struct vl53l4cd_data *data = dev->data;

    if (chan != SENSOR_CHAN_DISTANCE) {
        return -ENOTSUP;
    }

    /* Zephyr uses meters in sensor_value for distance */
    int32_t mm = data->distance_mm;
    if (!data->data_valid) {
        return -EAGAIN;
    }

    /* Convert mm -> m: val = mm / 1000.0 */
    val->val1 = mm / 1000;                     /* integer meters */
    val->val2 = (mm % 1000) * 1000000 / 1000;  /* remaining in nanometers */
    return 0;
}

static int vl53l4cd_init(const struct device *dev)
{
    const struct vl53l4cd_config *cfg = dev->config;
    struct vl53l4cd_data *data = dev->data;

    /* ----- I2C bus ----- */
    data->i2c = device_get_binding(cfg->i2c_bus_label);
    if (!data->i2c) {
        LOG_ERR("I2C bus '%s' not found", cfg->i2c_bus_label);
        return -ENODEV;
    }

    /* ----- Platform bridge for ULD ----- */
    data->plat.i2c      = data->i2c;
    data->plat.i2c_addr = cfg->i2c_addr;

    /* ----- Optional XSHUT reset ----- */
    if (cfg->xshut_present) {
        if (!device_is_ready(cfg->xshut.port)) {
            LOG_ERR("XSHUT GPIO controller not ready");
            return -ENODEV;
        }

        /* Carry pull configs from DT if present, configure as output */
        gpio_flags_t flags = GPIO_OUTPUT;
        if (cfg->xshut.dt_flags & GPIO_PULL_UP)   { flags |= GPIO_PULL_UP; }
        if (cfg->xshut.dt_flags & GPIO_PULL_DOWN) { flags |= GPIO_PULL_DOWN; }

        int r = gpio_pin_configure(cfg->xshut.port, cfg->xshut.pin, flags);
        if (r) {
            LOG_ERR("XSHUT configure failed (%d)", r);
            return r;
        }

        const bool active_low   = (cfg->xshut.dt_flags & GPIO_ACTIVE_LOW) != 0;
        const int  level_active = active_low ? 0 : 1; /* assert XSHUT (shutdown) */
        const int  level_idle   = active_low ? 1 : 0; /* deassert XSHUT (run)     */

        /* Pulse XSHUT to guarantee a clean startup */
        gpio_pin_set(cfg->xshut.port, cfg->xshut.pin, level_active);
        k_msleep(2);
        gpio_pin_set(cfg->xshut.port, cfg->xshut.pin, level_idle);
        k_msleep(2);
    }

    /* ----- ULD bring-up ----- */
    int st = VL53L4CD_SensorInit(&data->plat);
    if (st != 0) {
        LOG_ERR("VL53L4CD_SensorInit failed: %d", st);
        return -EIO;
    }

    /* Ranging timing (DT overrides, else use Kconfig default, else 50/0) */
#if defined(CONFIG_VL53L4CD_DEFAULT_TIMING_BUDGET_MS)
    uint16_t tb_ms = cfg->timing_budget_ms ? cfg->timing_budget_ms
                                           : CONFIG_VL53L4CD_DEFAULT_TIMING_BUDGET_MS;
#else
    uint8_t tb_ms = cfg->timing_budget_ms ? cfg->timing_budget_ms : 50;
#endif
    uint8_t im_ms = cfg->inter_measure_ms; /* 0 = back-to-back */

    data->timing_budget_ms = tb_ms;
    data->inter_measure_ms = im_ms;

    (void)VL53L4CD_SetRangeTiming(&data->plat, tb_ms, im_ms);

    st = VL53L4CD_StartRanging(&data->plat);
    if (st != 0) {
        LOG_ERR("VL53L4CD_StartRanging failed: %d", st);
        return -EIO;
    }

    LOG_INF("VL53L4CD ready on %s @ 0x%02x (tb=%u ms, im=%u ms)",
            cfg->i2c_bus_label, cfg->i2c_addr, tb_ms, im_ms);
    return 0;
}

/* Optional: no triggers/interrupts in this minimal version */
static const struct sensor_driver_api vl53l4cd_api = {
    .sample_fetch = vl53l4cd_sample_fetch,
    .channel_get  = vl53l4cd_channel_get,
};

int vl53l4cd_reinit(const struct device *dev)
{
    const struct vl53l4cd_config *cfg = dev->config;
    struct vl53l4cd_data *data = dev->data;
    int err = VL53L4CD_SensorInit(&data->plat);
    if (err) {
        LOG_ERR("Reinit: SensorInit=%d", err);
        return -EIO;
    }


    err = VL53L4CD_SetRangeTiming(&data->plat, data->timing_budget_ms, data->inter_measure_ms);
    if (err) {
        LOG_ERR("Reinit: SetRangeTiming=%d", err);
        return -EIO;
    }

    err = VL53L4CD_StartRanging(&data->plat);
    if (err) {
        LOG_ERR("Reinit: StartRanging=%d", err);
        return -EIO;
    }
    return 0;
}

int vl53l4cd_set_timing(const struct device *dev, uint8_t timing_budget_ms, uint8_t inter_measure_ms)
{
    struct vl53l4cd_data *data = dev->data;
    int err;

    if (timing_budget_ms < 10 || timing_budget_ms > 200) {
        LOG_ERR("SetTiming: invalid timing_budget_ms=%u, using default 50", timing_budget_ms);
        timing_budget_ms = 50;
    }
    /* Stop ranging before changing timing */
    err = VL53L4CD_StopRanging(&data->plat);
    if (err) {
        LOG_ERR("SetTiming: StopRanging=%d", err);
        return -EIO;
    }

    err = VL53L4CD_SetRangeTiming(&data->plat, timing_budget_ms, inter_measure_ms);
    if (err) {
        LOG_ERR("SetTiming: SetRangeTiming=%d", err);
        return -EIO;
    }

    data->timing_budget_ms = timing_budget_ms;
    data->inter_measure_ms = inter_measure_ms;

    err = VL53L4CD_StartRanging(&data->plat);
    if (err) {
        LOG_ERR("SetTiming: StartRanging=%d", err);
        return -EIO;
    }

    LOG_INF("Timing updated: tb=%u ms, im=%u ms", timing_budget_ms, inter_measure_ms);
    return 0;
}

int vl53l4cd_stop(const struct device *dev)
{
    struct vl53l4cd_data *data = dev->data;
    (void)VL53L4CD_StopRanging(&data->plat);
    return 0;
}

/* -------- Device instantiation via DT (Zephyr 2.6 style) -------- */

#define VL53L4CD_XSHUT_INIT(inst) \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, xshut_gpios), \
      ( .xshut = GPIO_DT_SPEC_INST_GET(inst, xshut_gpios), \
        .xshut_present = true, ), \
      ( .xshut_present = false, ))

#define VL53L4CD_INIT(inst)                                                       \
    static struct vl53l4cd_data vl53l4cd_data_##inst;                             \
                                                                                  \
    static const struct vl53l4cd_config vl53l4cd_config_##inst = {                \
        .i2c_bus_label   = DT_LABEL(DT_INST_BUS(inst)),                           \
        .i2c_addr        = DT_INST_REG_ADDR(inst),                                \
        .timing_budget_ms= DT_INST_PROP_OR(inst, timing_budget_ms, 50),           \
        .inter_measure_ms= DT_INST_PROP_OR(inst, inter_measure_ms, 0),            \
        VL53L4CD_XSHUT_INIT(inst)                                                 \
    };                                                                            \
                                                                                  \
    DEVICE_DT_INST_DEFINE(inst,                                                   \
                          vl53l4cd_init,                                          \
                          NULL,                                                   \
                          &vl53l4cd_data_##inst,                                  \
                          &vl53l4cd_config_##inst,                                \
                          POST_KERNEL,                                            \
                          CONFIG_SENSOR_INIT_PRIORITY,                            \
                          &vl53l4cd_api);

DT_INST_FOREACH_STATUS_OKAY(VL53L4CD_INIT)