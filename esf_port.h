/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esf_port.h
 * @brief esp-serial-flasher port backed by esp-ice's serial.h API.
 */
#ifndef ESF_PORT_H
#define ESF_PORT_H

#include <stdint.h>
#include "esp_loader_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration — struct serial is opaque, defined per-platform. */
struct serial;

/**
 * @brief Concrete ice serial port instance.
 *
 * Fill @c device and @c baudrate, set @c port.ops = &esf_port_ops,
 * then pass @c &p.port to esp_loader_init_uart().
 *
 * @code
 *   esf_port_t p = {
 *       .port.ops = &esf_port_ops,
 *       .device   = "/dev/ttyUSB0",
 *       .baudrate = 115200,
 *   };
 *   esp_loader_t loader;
 *   esp_loader_init_uart(&loader, &p.port);
 * @endcode
 */
typedef struct {
	esp_loader_port_t  port;     /*!< Embedded base; pass &port to esp_loader_init_* */

	/* Public configuration — fill before calling esp_loader_init_uart() */
	const char        *device;  /*!< Serial device path, e.g. "/dev/ttyUSB0" */
	unsigned           baudrate;/*!< Initial baud rate, e.g. 115200 */

	/* Private runtime state — do not access directly */
	struct serial     *_serial; /*!< Opened by init, closed by deinit */
	int64_t            _time_end;/*!< Deadline set by start_timer, ms from time_now_ms() epoch */
} esf_port_t;

/** Operations vtable for esf_port_t. */
extern const esp_loader_port_ops_t esf_port_ops;

#ifdef __cplusplus
}
#endif

#endif /* ESF_PORT_H */
