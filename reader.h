/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file reader.h
 * @brief Streaming byte-reader abstraction with codec support.
 *
 * A reader is a unidirectional pull-model byte source.  The caller
 * asks for bytes via r->read(); the underlying implementation (plain
 * file, gzip-decompressed, xz-decompressed) is opaque.
 *
 * This lets format parsers (tar, and eventually zip's per-entry
 * streams) be written once without caring whether the input is
 * compressed or what codec was used.
 *
 * The reader opens the file itself (via fopen() -- wrapped on
 * Windows for UTF-8 paths) and closes it in reader_close().  All I/O
 * goes through stdio, so the code is portable to Windows with no
 * POSIX headers.
 *
 * Usage:
 *   struct reader *r = reader_open_xz(path);
 *   if (!r) return -1;
 *   uint8_t buf[512];
 *   ssize_t n = r->read(r, buf, sizeof(buf));
 *   reader_close(r);
 */
#ifndef READER_H
#define READER_H

#include <stddef.h>
#include <sys/types.h>

struct reader {
	/**
	 * @brief Read up to @p len decoded bytes into @p buf.
	 *
	 * Returns the number of bytes actually placed in @p buf:
	 *   > 0  bytes produced
	 *     0  end of stream (no more data, no error)
	 *    -1  I/O or decode error
	 *
	 * May produce fewer bytes than requested even before EOF; callers
	 * that need an exact count should use reader_read_exact().
	 */
	ssize_t (*read)(struct reader *r, void *buf, size_t len);

	/**
	 * @brief Release decoder state and close the underlying file.
	 */
	void (*close)(struct reader *r);
};

/**
 * @brief Open a reader that reads @p path plainly (no decompression).
 * @return reader on success, NULL on fopen failure.
 */
struct reader *reader_open_plain(const char *path);

/**
 * @brief Open a reader that gzip-decompresses @p path (zlib).
 * @return reader on success, NULL on fopen failure.
 */
struct reader *reader_open_gzip(const char *path);

/**
 * @brief Open a reader that xz-decompresses @p path (XZ Embedded).
 * @return reader on success, NULL on fopen failure.
 */
struct reader *reader_open_xz(const char *path);

/**
 * @brief Read exactly @p len bytes, looping on short reads.
 *
 * @return 0 on success (exactly @p len bytes read), -1 on I/O error
 *         or premature EOF.
 */
int reader_read_exact(struct reader *r, void *buf, size_t len);

/**
 * @brief Read and discard @p n decoded bytes.
 *
 * @return 0 on success, -1 on I/O error or premature EOF.
 */
int reader_skip(struct reader *r, size_t n);

/** Convenience wrapper for r->close(r). */
static inline void reader_close(struct reader *r) { r->close(r); }

#endif /* READER_H */
