/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file platform/win/serial.c
 * @brief Windows stub for @ref serial.h.
 *
 * Placeholder until the Win32 Comm API backend lands.  Every entry
 * point returns @c -ENOSYS so callers that accidentally try to open
 * a serial port on Windows get a clean "not implemented" failure
 * instead of an unresolved symbol at link time.  Replace each body
 * with the real @c CreateFileA / @c SetCommState / @c ReadFile /
 * @c WriteFile / @c EscapeCommFunction call when wiring up Windows
 * serial support.
 */
#include <errno.h>
#include <string.h>

#include "serial.h"

void serial_init(struct serial *s) { memset(s, 0, sizeof(*s)); }

int serial_open(struct serial *s, const char *path)
{
	(void)s;
	(void)path;
	return -ENOSYS;
}

void serial_close(struct serial *s) { (void)s; }

int serial_set_baud(struct serial *s, unsigned baud)
{
	(void)s;
	(void)baud;
	return -ENOSYS;
}

ssize_t serial_read(struct serial *s, void *buf, size_t n, unsigned timeout_ms)
{
	(void)s;
	(void)buf;
	(void)n;
	(void)timeout_ms;
	errno = ENOSYS;
	return -1;
}

ssize_t serial_write(struct serial *s, const void *buf, size_t n)
{
	(void)s;
	(void)buf;
	(void)n;
	errno = ENOSYS;
	return -1;
}

int serial_set_dtr(struct serial *s, int on)
{
	(void)s;
	(void)on;
	return -ENOSYS;
}

int serial_set_rts(struct serial *s, int on)
{
	(void)s;
	(void)on;
	return -ENOSYS;
}

int serial_flush_input(struct serial *s)
{
	(void)s;
	return -ENOSYS;
}
