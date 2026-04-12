/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sbuf.h
 * @brief Dynamic NUL-terminated string buffer.
 *
 * sbuf manages a heap-allocated, NUL-terminated byte string that
 * grows on demand. The buffer pointer is never NULL -- when empty it
 * points to a static empty string, so sb->buf is always a valid C
 * string. All allocating operations die() on failure.
 *
 * Usage:
 *   struct sbuf sb = SBUF_INIT;
 *   sbuf_addf(&sb, "%s/%s", dir, "build.ninja");
 *   printf("%s\n", sb.buf);
 *   sbuf_release(&sb);
 */
#ifndef SBUF_H
#define SBUF_H

#include <stdarg.h>
#include <stddef.h>

#include "platform.h"

/** Sentinel empty string -- sb->buf points here when no allocation. */
extern char sbuf_empty[];

struct sbuf {
	char *buf;	/**< NUL-terminated buffer (never NULL). */
	size_t len;	/**< Current string length (excluding NUL). */
	size_t alloc;	/**< Allocated bytes (0 = not owned). */
};

/** Static initializer. */
#define SBUF_INIT { .buf = sbuf_empty, .len = 0, .alloc = 0 }

/** Initialize an sbuf (equivalent to SBUF_INIT assignment). */
void sbuf_init(struct sbuf *sb);

/** Free the buffer and reset to empty state. */
void sbuf_release(struct sbuf *sb);

/** Detach and return the buffer; caller must free(). Resets sb to empty. */
char *sbuf_detach(struct sbuf *sb);

/**
 * @brief Ensure at least @p extra bytes available after sb->len.
 *
 * After this call, writing up to @p extra bytes at sb->buf + sb->len
 * is safe (the NUL terminator is accounted for).
 */
void sbuf_grow(struct sbuf *sb, size_t extra);

/** Reset length to 0 without freeing (keeps allocation for reuse). */
static inline void sbuf_reset(struct sbuf *sb)
{
	sb->len = 0;
	if (sb->alloc)
		sb->buf[0] = '\0';
}

/** Set length to @p len and NUL-terminate. Caller must ensure len <= alloc. */
static inline void sbuf_setlen(struct sbuf *sb, size_t len)
{
	sb->len = len;
	sb->buf[len] = '\0';
}

/** Append a single byte. */
static inline void sbuf_addch(struct sbuf *sb, int c)
{
	sbuf_grow(sb, 1);
	sb->buf[sb->len++] = (char)c;
	sb->buf[sb->len] = '\0';
}

/** Append raw bytes. */
void sbuf_add(struct sbuf *sb, const void *data, size_t len);

/** Append a NUL-terminated string. */
void sbuf_addstr(struct sbuf *sb, const char *s);

/** Append a printf-formatted string. */
void sbuf_addf(struct sbuf *sb, const char *fmt, ...);

/** Append a printf-formatted string (va_list variant). */
void sbuf_vaddf(struct sbuf *sb, const char *fmt, va_list ap);

/** Read entire file contents into the buffer. Returns bytes read or -1. */
ssize_t sbuf_read_file(struct sbuf *sb, const char *path);

/** Remove trailing whitespace. */
void sbuf_rtrim(struct sbuf *sb);

/** Duplicate a NUL-terminated string. Caller must free(). Dies on OOM. */
char *sbuf_strdup(const char *s);

/** Duplicate at most @p n bytes of @p s. Caller must free(). Dies on OOM. */
char *sbuf_strndup(const char *s, size_t n);

#endif /* SBUF_H */
