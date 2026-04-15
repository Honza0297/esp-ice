/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file xz.c
 * @brief Glue layer: liblzma decoder presented as a struct reader.
 *
 * Mirrors gzip.c but uses lzma_code() instead of inflate().  The
 * LZMA_CONCATENATED flag handles multi-stream .xz files that some
 * parallel xz encoders (pxz, pixz) produce.
 */

#include <lzma.h>
#include <stdint.h>

#include "ice.h"
#include "reader.h"

#define XZ_IN_BUF 8192

struct xz_reader {
	struct reader base;
	FILE *fp;
	lzma_stream strm;
	uint8_t in_buf[XZ_IN_BUF];
	int input_eof;
	int stream_eof;
};

static ssize_t xz_reader_read(struct reader *base, void *out, size_t out_len)
{
	struct xz_reader *r = (struct xz_reader *)base;

	if (r->stream_eof)
		return 0;

	r->strm.next_out = out;
	r->strm.avail_out = out_len;

	while (r->strm.avail_out > 0 && !r->stream_eof) {
		if (r->strm.avail_in == 0 && !r->input_eof) {
			size_t n =
			    fread(r->in_buf, 1, sizeof(r->in_buf), r->fp);
			if (n == 0) {
				if (ferror(r->fp))
					return -1;
				r->input_eof = 1;
			} else {
				r->strm.next_in = r->in_buf;
				r->strm.avail_in = n;
			}
		}

		lzma_ret ret = lzma_code(&r->strm, LZMA_RUN);
		if (ret == LZMA_STREAM_END) {
			r->stream_eof = 1;
			break;
		}
		if (ret != LZMA_OK) {
			/* LZMA_BUF_ERROR (starved at EOF), LZMA_DATA_ERROR,
			 * LZMA_MEM_ERROR, LZMA_FORMAT_ERROR, etc. */
			return -1;
		}
	}

	return (ssize_t)(out_len - r->strm.avail_out);
}

static void xz_reader_close(struct reader *base)
{
	struct xz_reader *r = (struct xz_reader *)base;
	lzma_end(&r->strm);
	if (r->fp)
		fclose(r->fp);
	free(r);
}

struct reader *reader_open_xz(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return NULL;

	struct xz_reader *r = calloc(1, sizeof(*r));
	if (!r)
		die_errno("calloc");
	r->base.read = xz_reader_read;
	r->base.close = xz_reader_close;
	r->fp = fp;

	/* UINT64_MAX = no memory cap; LZMA_CONCATENATED = walk through
	 * multiple .xz streams in the same file (pxz / pixz output). */
	if (lzma_stream_decoder(&r->strm, UINT64_MAX, LZMA_CONCATENATED) !=
	    LZMA_OK)
		die("lzma_stream_decoder failed");
	return &r->base;
}
