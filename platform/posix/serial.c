/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file platform/posix/serial.c
 * @brief POSIX termios implementation of @ref serial.h.
 *
 * Covers Linux and macOS.  Baud rates are resolved through a table
 * of @c B<rate> constants; non-standard rates return @c -EINVAL.  On
 * Linux the @c BOTHER / @c TCSETS2 path would let us set arbitrary
 * integer rates, but every baud the ESPLoader protocol actually uses
 * (115200, 230400, 460800, 921600, 1500000) has a @c B constant so we
 * do not need that complication yet.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "serial.h"

/* ------------------------------------------------------------------ */
/*  Baud-rate table                                                    */
/* ------------------------------------------------------------------ */

struct baud_entry {
	unsigned baud;
	speed_t speed;
};

static const struct baud_entry baud_table[] = {
    {9600, B9600},
    {19200, B19200},
    {38400, B38400},
    {57600, B57600},
    {115200, B115200},
    {230400, B230400},
#ifdef B460800
    {460800, B460800},
#endif
#ifdef B500000
    {500000, B500000},
#endif
#ifdef B576000
    {576000, B576000},
#endif
#ifdef B921600
    {921600, B921600},
#endif
#ifdef B1000000
    {1000000, B1000000},
#endif
#ifdef B1500000
    {1500000, B1500000},
#endif
#ifdef B2000000
    {2000000, B2000000},
#endif
    {0, 0},
};

static speed_t baud_lookup(unsigned baud)
{
	for (const struct baud_entry *e = baud_table; e->baud; e++)
		if (e->baud == baud)
			return e->speed;
	return 0;
}

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

void serial_init(struct serial *s)
{
	memset(s, 0, sizeof(*s));
	s->fd = -1;
}

int serial_open(struct serial *s, const char *path)
{
	struct termios tio;
	int fd;

	serial_init(s);

	fd = open(path, O_RDWR | O_NOCTTY);
	if (fd < 0)
		return -errno;

	/* FD_CLOEXEC via fcntl — portable across POSIX.1-2001 and later
	 * (the O_CLOEXEC open flag requires POSIX.1-2008 which ice's
	 * -D_POSIX_C_SOURCE=200112L does not expose). */
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		int err = -errno;

		close(fd);
		return err;
	}

	if (tcgetattr(fd, &tio) < 0) {
		int err = -errno;

		close(fd);
		return err;
	}

	/*
	 * Raw mode: clear every input/output/line-discipline bit that
	 * could alter the byte stream, then force 8 bits / no parity /
	 * one stop bit / receiver enabled / ignore modem status.
	 */
	tio.c_iflag &= (tcflag_t) ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
				    IGNCR | ICRNL | IXON | IXOFF | IXANY);
	tio.c_oflag &= (tcflag_t)~OPOST;
	tio.c_lflag &=
	    (tcflag_t) ~(ECHO | ECHOE | ECHONL | ICANON | ISIG | IEXTEN);
	tio.c_cflag &= (tcflag_t) ~(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
	tio.c_cflag &= (tcflag_t)~CRTSCTS;
#endif
	tio.c_cflag |= CS8 | CREAD | CLOCAL;

	/*
	 * Timeouts are enforced via select() in serial_read(); configure
	 * the terminal for immediate returns so read() does not block.
	 */
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &tio) < 0) {
		int err = -errno;

		close(fd);
		return err;
	}

	s->fd = fd;
	strncpy(s->path, path, sizeof s->path - 1);
	s->path[sizeof s->path - 1] = '\0';
	return 0;
}

void serial_close(struct serial *s)
{
	if (s->fd >= 0) {
		close(s->fd);
		s->fd = -1;
	}
}

int serial_set_baud(struct serial *s, unsigned baud)
{
	struct termios tio;
	speed_t speed = baud_lookup(baud);

	if (speed == 0)
		return -EINVAL;
	if (tcgetattr(s->fd, &tio) < 0)
		return -errno;
	if (cfsetispeed(&tio, speed) < 0)
		return -errno;
	if (cfsetospeed(&tio, speed) < 0)
		return -errno;
	if (tcsetattr(s->fd, TCSANOW, &tio) < 0)
		return -errno;

	s->baud = baud;
	return 0;
}

ssize_t serial_read(struct serial *s, void *buf, size_t n, unsigned timeout_ms)
{
	fd_set rfds;
	struct timeval tv;
	int rc;

	FD_ZERO(&rfds);
	FD_SET(s->fd, &rfds);

	tv.tv_sec = (long)(timeout_ms / 1000u);
	tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);

	do {
		rc = select(s->fd + 1, &rfds, NULL, NULL, &tv);
	} while (rc < 0 && errno == EINTR);

	if (rc < 0)
		return -1;
	if (rc == 0)
		return 0; /* timeout */

	return read(s->fd, buf, n);
}

ssize_t serial_write(struct serial *s, const void *buf, size_t n)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t total = 0;

	while (total < n) {
		ssize_t w = write(s->fd, p + total, n - total);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		total += (size_t)w;
	}
	return (ssize_t)total;
}

static int set_modem_bit(int fd, int bit, int on)
{
	int bits = bit;

	if (ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &bits) < 0)
		return -errno;
	return 0;
}

int serial_set_dtr(struct serial *s, int on)
{
	return set_modem_bit(s->fd, TIOCM_DTR, on);
}

int serial_set_rts(struct serial *s, int on)
{
	return set_modem_bit(s->fd, TIOCM_RTS, on);
}

int serial_flush_input(struct serial *s)
{
	if (tcflush(s->fd, TCIFLUSH) < 0)
		return -errno;
	return 0;
}
