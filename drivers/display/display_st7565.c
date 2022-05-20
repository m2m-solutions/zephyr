/*
 * Copyright (c) 2017 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2019 Marc Reilly
 * Copyright (c) 2019 PHYTEC Messtechnik GmbH
 * Copyright (c) 2020 Endian Technologies AB
 * Copyright (c) 2022 Mark Olsson <mark@markolsson.se>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT sitronix_st7565

#include "display_st7565.h"

#include <device.h>
#include <drivers/spi.h>
#include <drivers/gpio.h>
#include <pm/device.h>
#include <sys/byteorder.h>
#include <drivers/display.h>

#define LOG_LEVEL CONFIG_DISPLAY_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(display_st7565);

#define ST7565_CS_PIN DT_INST_SPI_DEV_CS_GPIOS_PIN(0)
#define ST7565_CMD_DATA_PIN DT_INST_GPIO_PIN(0, cmd_data_gpios)
#define ST7565_CMD_DATA_FLAGS DT_INST_GPIO_FLAGS(0, cmd_data_gpios)
#define ST7565_RESET_PIN DT_INST_GPIO_PIN(0, reset_gpios)
#define ST7565_RESET_FLAGS DT_INST_GPIO_FLAGS(0, reset_gpios)

#define READBIT(byte, index) (((unsigned)(byte) >> (index)) & 1)

const uint8_t pagemap[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#define ST7565_STARTBYTES 4

static uint8_t st7565_framebuffer[1024];

struct st7565_data {
	const struct device *spi_dev;
	struct spi_config spi_config;
#if DT_INST_SPI_DEV_HAS_CS_GPIOS(0)
	struct spi_cs_control cs_ctrl;
#endif

#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	const struct device *reset_gpio;
#endif
	const struct device *cmd_data_gpio;

	uint16_t height;
	uint16_t width;
	uint16_t x_offset;
	uint16_t y_offset;
};

static void st7565_set_cmd(const struct device *dev, int is_cmd)
{
	struct st7565_data *data = dev->data;

	gpio_pin_set(data->cmd_data_gpio, ST7565_CMD_DATA_PIN, is_cmd);
}

static void st7565_transmit(const struct device *dev, uint8_t cmd)
{
	struct st7565_data *data = dev->data;

	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

	st7565_set_cmd(dev, 1);
	spi_write(data->spi_dev, &data->spi_config, &tx_bufs);
}

static void st7565_reset_display(const struct device *dev)
{
	LOG_DBG("Resetting display");
	struct st7565_data *data = dev->data;
	k_sleep(K_MSEC(10));
	gpio_pin_set(data->reset_gpio, ST7565_RESET_PIN, 1);
	k_sleep(K_MSEC(60));
	gpio_pin_set(data->reset_gpio, ST7565_RESET_PIN, 0);
	k_sleep(K_MSEC(50));
}

static int st7565_blanking_on(const struct device *dev)
{
	LOG_INF("Blanking on");
	st7565_transmit(dev, ST7565_CMD_DISPLAY_OFF);
	return 0;
}

static int st7565_blanking_off(const struct device *dev)
{
	LOG_INF("Blanking off");
	st7565_transmit(dev, ST7565_CMD_DISPLAY_ON);
	return 0;
}

static int st7565_read(const struct device *dev, const uint16_t x, const uint16_t y,
		       const struct display_buffer_descriptor *desc, void *buf)
{
	return -ENOTSUP;
}

static void st7565_sync(const struct device *dev)
{
	struct st7565_data *data = dev->data;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };
	for (uint8_t p = 0; p < sizeof(pagemap); p++) {
		st7565_transmit(dev, ST7565_CMD_SET_PAGE | pagemap[p]);
		st7565_transmit(dev, ST7565_CMD_SET_COLUMN_LOWER | (ST7565_STARTBYTES & 0xf));
		st7565_transmit(dev,
				ST7565_CMD_SET_COLUMN_UPPER | ((ST7565_STARTBYTES >> 4) & 0x0F));
		st7565_set_cmd(dev, 0);
		tx_buf.buf = (void *)&st7565_framebuffer[p * 128];
		tx_buf.len = 128;
		spi_write(data->spi_dev, &data->spi_config, &tx_bufs);
	}
}

static int st7565_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	struct st7565_data *data = dev->data;
	uint32_t first_dest_byte = x + (y * data->width / 8);
	memcpy(st7565_framebuffer + first_dest_byte, buf, desc->buf_size);
	st7565_sync(dev);
	return 0;
}

static void *st7565_get_framebuffer(const struct device *dev)
{
	return NULL;
}

static int st7565_set_brightness(const struct device *dev, const uint8_t brightness)
{
	return -ENOTSUP;
}

static int st7565_set_contrast(const struct device *dev, const uint8_t contrast)
{
	LOG_INF("Set Contrast %d", contrast);
	st7565_transmit(dev, ST7565_CMD_SET_VOLUME_FIRST);
	st7565_transmit(dev, ST7565_CMD_SET_VOLUME_SECOND | (contrast & 0x3f));

	return -ENOTSUP;
}

static void st7565_get_capabilities(const struct device *dev,
				    struct display_capabilities *capabilities)
{
	struct st7565_data *data = dev->data;

	memset(capabilities, 0, sizeof(struct display_capabilities));
	capabilities->x_resolution = data->width;
	capabilities->y_resolution = data->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_MONO01;
	capabilities->current_pixel_format = PIXEL_FORMAT_MONO01;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
	capabilities->screen_info = SCREEN_INFO_MONO_VTILED;
}

static int st7565_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pixel_format)
{
	if (pixel_format == PIXEL_FORMAT_MONO01) {
		return 0;
	}
	LOG_ERR("Pixel format change not implemented");
	return -ENOTSUP;
}

static int st7565_set_orientation(const struct device *dev,
				  const enum display_orientation orientation)
{
	if (orientation == DISPLAY_ORIENTATION_NORMAL) {
		return 0;
	}
	LOG_ERR("Changing display orientation not implemented");
	return -ENOTSUP;
}

static void st7565_lcd_init(const struct device *dev)
{
	// struct st7565_data *data = dev->data;

	st7565_transmit(dev, ST7565_CMD_INTERNAL_RESET);
	// LCD bias select
	st7565_transmit(dev, ST7565_CMD_SET_BIAS_7);
	// ADC select
	st7565_transmit(dev, ST7565_CMD_SET_ADC_NORMAL);
	// SHL select
	st7565_transmit(dev, ST7565_CMD_SET_COM_REVERSE);
	// Initial display line
	st7565_transmit(dev, ST7565_CMD_SET_DISP_START_LINE);

	// turn on voltage converter (VC=1, VR=0, VF=0)
	st7565_transmit(dev, ST7565_CMD_SET_POWER_CONTROL | 0x4);
	// wait for 50% rising
	k_sleep(K_MSEC(50));

	// turn on voltage regulator (VC=1, VR=1, VF=0)
	st7565_transmit(dev, ST7565_CMD_SET_POWER_CONTROL | 0x6);
	// wait >=50ms
	k_sleep(K_MSEC(50));

	// turn on voltage follower (VC=1, VR=1, VF=1)
	st7565_transmit(dev, ST7565_CMD_SET_POWER_CONTROL | 0x7);
	// wait
	k_sleep(K_MSEC(10));

	// set lcd operating voltage (regulator resistor, ref voltage resistor)
	st7565_transmit(dev, ST7565_CMD_SET_RESISTOR_RATIO | 0x7);

	st7565_transmit(dev, ST7565_CMD_DISPLAY_ON);
	st7565_transmit(dev, ST7565_CMD_SET_ALLPTS_NORMAL);

	st7565_set_contrast(dev, 0x0);

}

static int st7565_init(const struct device *dev)
{
	struct st7565_data *data = dev->data;

	memset(st7565_framebuffer, 0x00, sizeof(st7565_framebuffer));
	
	data->spi_dev = device_get_binding(DT_INST_BUS_LABEL(0));
	if (data->spi_dev == NULL) {
		LOG_ERR("Could not get SPI device for LCD");
		return -EPERM;
	}

	data->spi_config.frequency = DT_INST_PROP(0, spi_max_frequency);
	data->spi_config.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8);
	data->spi_config.slave = DT_INST_REG_ADDR(0);

#if DT_INST_SPI_DEV_HAS_CS_GPIOS(0)
	data->cs_ctrl.gpio_dev = device_get_binding(DT_INST_SPI_DEV_CS_GPIOS_LABEL(0));
	data->cs_ctrl.gpio_pin = DT_INST_SPI_DEV_CS_GPIOS_PIN(0);
	data->cs_ctrl.gpio_dt_flags = DT_INST_SPI_DEV_CS_GPIOS_FLAGS(0);
	data->cs_ctrl.delay = 0U;
	data->spi_config.cs = &(data->cs_ctrl);
#else
	data->spi_config.cs = NULL;
#endif

#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	data->reset_gpio = device_get_binding(DT_INST_GPIO_LABEL(0, reset_gpios));
	if (data->reset_gpio == NULL) {
		LOG_ERR("Could not get GPIO port for display reset");
		return -EPERM;
	}

	if (gpio_pin_configure(data->reset_gpio, ST7565_RESET_PIN,
			       GPIO_OUTPUT_INACTIVE | ST7565_RESET_FLAGS)) {
		LOG_ERR("Couldn't configure reset pin");
		return -EIO;
	}
#endif

	data->cmd_data_gpio = device_get_binding(DT_INST_GPIO_LABEL(0, cmd_data_gpios));
	if (data->cmd_data_gpio == NULL) {
		LOG_ERR("Could not get GPIO port for cmd/DATA port");
		return -EPERM;
	}
	if (gpio_pin_configure(data->cmd_data_gpio, ST7565_CMD_DATA_PIN,
			       GPIO_OUTPUT | ST7565_CMD_DATA_FLAGS)) {
		LOG_ERR("Couldn't configure cmd/DATA pin");
		return -EIO;
	}

	st7565_reset_display(dev);

	st7565_lcd_init(dev);

	st7565_blanking_on(dev);

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int st7565_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret = 0;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		// ??
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		// ??
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

static const struct display_driver_api st7565_api = {
	.blanking_on = st7565_blanking_on,
	.blanking_off = st7565_blanking_off,
	.write = st7565_write,
	.read = st7565_read,
	.get_framebuffer = st7565_get_framebuffer,
	.set_brightness = st7565_set_brightness,
	.set_contrast = st7565_set_contrast,
	.get_capabilities = st7565_get_capabilities,
	.set_pixel_format = st7565_set_pixel_format,
	.set_orientation = st7565_set_orientation,
};

static struct st7565_data st7565_data = {
	.width = DT_INST_PROP(0, width),
	.height = DT_INST_PROP(0, height),
};

PM_DEVICE_DT_INST_DEFINE(0, st7565_pm_action);

DEVICE_DT_INST_DEFINE(0, &st7565_init, PM_DEVICE_DT_INST_GET(0), &st7565_data, NULL, POST_KERNEL,
		      CONFIG_DISPLAY_INIT_PRIORITY, &st7565_api);
