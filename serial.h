/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file serial.h
 * @brief Small portable serial-port API (POSIX + Windows).
 *
 * Wraps the bare minimum of @c termios (POSIX) and the Win32 Comm API
 * (Windows) that the ESPLoader protocol needs: open a device by path,
 * set the baud rate, read with a millisecond timeout, write blocking,
 * toggle DTR/RTS for the reset sequence, and drop the input buffer.
 * Everything is 8N1 / no flow control -- that is all ESP chips speak.
 *
 * The Windows backend is a stub returning -ENOSYS today; the POSIX
 * backend is the real thing, used on Linux and macOS.  A Windows
 * implementation lands when we first need to run serial tests on
 * Windows CI.
 *
 * Error convention:
 *   - Setup functions return 0 on success or @c -errno on failure.
 *   - @ref serial_read / @ref serial_write return the number of bytes
 *     transferred (>= 0) or -1 with @c errno set on failure.
 *     @ref serial_read returns 0 on timeout.
 *
 * Callers decide whether to propagate, retry, or @c die_errno() on
 * failure.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>

/* ssize_t: POSIX has it in <sys/types.h>; Windows CRT does not, so
 * we typedef it to match platform.h's Windows shim.  Including
 * platform.h itself from here would drag in a lot of stdio / color
 * machinery this header doesn't need. */
#ifdef _WIN32
typedef ptrdiff_t ssize_t;
#else
#include <sys/types.h>
#endif

/**
 * Serial port descriptor.  Opaque to callers in spirit, but defined
 * here so it can live on the stack.  Reset via @ref serial_init or
 * the @ref SERIAL_INIT initialiser before use.
 */
struct serial {
#ifdef _WIN32
	void *handle; /**< HANDLE on Windows; NULL when closed. */
#else
	int fd; /**< File descriptor; -1 when closed. */
#endif
	unsigned baud;	/**< Last baud rate set (0 = not set). */
	char path[256]; /**< Copy of the opened path, for error messages. */
};

#ifdef _WIN32
#define SERIAL_INIT {.handle = 0, .baud = 0, .path = {0}}
#else
#define SERIAL_INIT {.fd = -1, .baud = 0, .path = {0}}
#endif

/** Reset a struct serial to the closed state (no I/O performed). */
void serial_init(struct serial *s);

/**
 * @brief Open @p path as an 8N1 raw serial device.
 *
 * Leaves DTR/RTS in whatever state the kernel assigned at open time
 * (which on Linux typically pulses DTR high — callers that care about
 * keeping the chip out of reset should follow up with
 * @ref serial_set_dtr immediately).  Also clears local echo, canonical
 * input, and output processing; and sets the buffer to non-blocking
 * so @ref serial_read can enforce its own timeout with @c select().
 *
 * @return 0 on success, -errno on failure.
 */
int serial_open(struct serial *s, const char *path);

/** Close the port.  Safe to call on an already-closed handle. */
void serial_close(struct serial *s);

/**
 * @brief Change the baud rate.
 *
 * Only rates that map to a standard @c B<rate> termios constant are
 * supported today (9600 / 19200 / 38400 / 57600 / 115200 / 230400,
 * plus 460800 / 500000 / 921600 / 1500000 / 2000000 on Linux).  For
 * rates outside that set, @c -EINVAL is returned; extend the internal
 * table when hardware testing reveals a need.
 *
 * @return 0 on success, -errno on failure.
 */
int serial_set_baud(struct serial *s, unsigned baud);

/**
 * @brief Read up to @p n bytes, waiting up to @p timeout_ms.
 *
 * @return bytes read (> 0), 0 on timeout, -1 on error (errno set).
 */
ssize_t serial_read(struct serial *s, void *buf, size_t n, unsigned timeout_ms);

/**
 * @brief Write @p n bytes, retrying on partial writes and @c EINTR.
 * @return bytes written on success (always @p n), -1 on error.
 */
ssize_t serial_write(struct serial *s, const void *buf, size_t n);

/**
 * @brief Assert (@p on != 0) or deassert the DTR modem line.
 * @return 0 on success, -errno on failure.
 */
int serial_set_dtr(struct serial *s, int on);

/**
 * @brief Assert (@p on != 0) or deassert the RTS modem line.
 * @return 0 on success, -errno on failure.
 */
int serial_set_rts(struct serial *s, int on);

/**
 * @brief Discard any bytes currently buffered on the input side.
 *
 * Used after the reset sequence to drop boot-time garbage from the
 * ROM before running the sync handshake.
 *
 * @return 0 on success, -errno on failure.
 */
int serial_flush_input(struct serial *s);

#endif /* SERIAL_H */
