/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file gzip.c
 * @brief Glue layer: zlib inflate presented as a struct reader.
 *
 * Uses inflateInit2(..., 15 + 16) which selects gzip-only framing
 * (as opposed to zlib stream or auto-detect).  That matches the
 * .tar.gz / .tgz archives we're decompressing.
 */

#include <stdint.h>
#include <zlib.h>

#include "ice.h"
#include "reader.h"

#define GZIP_IN_BUF 8192

struct gzip_reader {
	struct reader base;
	FILE *fp;
	z_stream zs;
	uint8_t in_buf[GZIP_IN_BUF];
	int input_eof;
	int stream_eof;
};

static ssize_t gzip_reader_read(struct reader *base, void *out, size_t out_len)
{
	struct gzip_reader *r = (struct gzip_reader *)base;

	if (r->stream_eof)
		return 0;

	r->zs.next_out = out;
	r->zs.avail_out = (uInt)out_len;

	while (r->zs.avail_out > 0 && !r->stream_eof) {
		if (r->zs.avail_in == 0 && !r->input_eof) {
			size_t n =
			    fread(r->in_buf, 1, sizeof(r->in_buf), r->fp);
			if (n == 0) {
				if (ferror(r->fp))
					return -1;
				r->input_eof = 1;
			} else {
				r->zs.next_in = r->in_buf;
				r->zs.avail_in = (uInt)n;
			}
		}

		int ret = inflate(&r->zs, Z_NO_FLUSH);
		if (ret == Z_STREAM_END) {
			r->stream_eof = 1;
			break;
		}
		if (ret != Z_OK) {
			/* Z_BUF_ERROR (starved at EOF), Z_DATA_ERROR,
			 * Z_MEM_ERROR, Z_NEED_DICT -- all fatal here. */
			return -1;
		}
	}

	return (ssize_t)(out_len - r->zs.avail_out);
}

static void gzip_reader_close(struct reader *base)
{
	struct gzip_reader *r = (struct gzip_reader *)base;
	inflateEnd(&r->zs);
	if (r->fp)
		fclose(r->fp);
	free(r);
}

struct reader *reader_open_gzip(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return NULL;

	struct gzip_reader *r = calloc(1, sizeof(*r));
	if (!r)
		die_errno("calloc");
	r->base.read = gzip_reader_read;
	r->base.close = gzip_reader_close;
	r->fp = fp;
	/* 15 = max window bits; +16 selects gzip-only framing. */
	if (inflateInit2(&r->zs, 15 + 16) != Z_OK)
		die("inflateInit2 failed");
	return &r->base;
}
