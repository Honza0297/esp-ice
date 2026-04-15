/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file reader.c
 * @brief reader abstraction: plain-file impl + shared helpers.
 *
 * All I/O goes through stdio (fopen/fread/fclose) so the code is
 * portable to Windows -- the platform layer wraps fopen() for UTF-8
 * paths on that OS.  Codec-backed readers (gzip, xz) live in their
 * own translation units so their dependencies stay isolated.
 */

#include <stdint.h>

#include "ice.h"
#include "reader.h"

struct plain_reader {
	struct reader base;
	FILE *fp;
};

static ssize_t plain_reader_read(struct reader *base, void *buf, size_t len)
{
	struct plain_reader *r = (struct plain_reader *)base;
	size_t n = fread(buf, 1, len, r->fp);
	if (n == 0 && ferror(r->fp))
		return -1;
	return (ssize_t)n;
}

static void plain_reader_close(struct reader *base)
{
	struct plain_reader *r = (struct plain_reader *)base;
	if (r->fp)
		fclose(r->fp);
	free(r);
}

struct reader *reader_open_plain(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return NULL;

	struct plain_reader *r = calloc(1, sizeof(*r));
	if (!r)
		die_errno("calloc");
	r->base.read = plain_reader_read;
	r->base.close = plain_reader_close;
	r->fp = fp;
	return &r->base;
}

int reader_read_exact(struct reader *r, void *buf, size_t len)
{
	uint8_t *p = buf;
	while (len > 0) {
		ssize_t n = r->read(r, p, len);
		if (n <= 0)
			return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

int reader_skip(struct reader *r, size_t n)
{
	uint8_t tmp[4096];
	while (n > 0) {
		size_t chunk = n < sizeof(tmp) ? n : sizeof(tmp);
		ssize_t got = r->read(r, tmp, chunk);
		if (got <= 0)
			return -1;
		n -= (size_t)got;
	}
	return 0;
}
