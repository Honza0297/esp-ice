/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file slip.h
 * @brief SLIP (RFC 1055) byte-stuffing framer used by ESPLoader.
 *
 * The protocol is tiny:
 *
 *   0xC0            frame delimiter (SLIP_END)
 *   0xDB 0xDC       escaped form of 0xC0
 *   0xDB 0xDD       escaped form of 0xDB
 *
 * ESPLoader wraps every command (to the chip) and every response
 * (from the chip) in a single SLIP frame, so we need exactly two
 * operations: emit a frame from a byte buffer, and decode an
 * incoming byte stream one byte at a time until a whole frame has
 * been reconstructed.
 *
 * ## Encoder
 *
 * @ref slip_encode appends a complete frame to a caller-provided
 * @c sbuf: leading 0xC0, then each byte of @p data with the two
 * escape substitutions applied, then trailing 0xC0.  Multiple calls
 * produce concatenated frames with the delimiters preserved.
 *
 * ## Decoder
 *
 * The decoder is a tiny state machine that consumes one byte per
 * call.  When a frame terminates, @ref slip_feed returns 1 and the
 * completed bytes are available at @c d->frame.buf / @c d->frame.len;
 * the caller is expected to process them and then call
 * @ref slip_decoder_reset before feeding more bytes.  A return of 0
 * means "more bytes needed", -1 means "protocol error" (an invalid
 * escape sequence).
 *
 * Typical usage:
 *
 *   struct slip_decoder d;
 *   slip_decoder_init(&d);
 *   for (;;) {
 *       uint8_t b;
 *       if (serial_read(s, &b, 1, timeout_ms) <= 0) break;
 *       int r = slip_feed(&d, b);
 *       if (r == 1) {
 *           // d.frame holds the decoded frame
 *           handle_frame(d.frame.buf, d.frame.len);
 *           slip_decoder_reset(&d);
 *       } else if (r < 0) {
 *           // protocol error; caller may reset and resync
 *           slip_decoder_reset(&d);
 *       }
 *   }
 *   slip_decoder_release(&d);
 */
#ifndef SLIP_H
#define SLIP_H

#include <stddef.h>
#include <stdint.h>

#include "sbuf.h"

/** SLIP delimiter bytes -- exposed for tests and protocol code. */
#define SLIP_END 0xC0u
#define SLIP_ESC 0xDBu
#define SLIP_ESC_END 0xDCu
#define SLIP_ESC_ESC 0xDDu

/**
 * @brief Append one SLIP frame to @p out.
 *
 * Writes 0xC0, then every byte of @p data with 0xC0 / 0xDB replaced
 * by their two-byte escaped forms, then a trailing 0xC0.
 *
 * @param out   sbuf to grow (existing contents are preserved).
 * @param data  raw payload.
 * @param n     number of bytes in @p data.
 */
void slip_encode(struct sbuf *out, const void *data, size_t n);

/**
 * Stateful byte-at-a-time decoder.  Zero-initialize via
 * @ref slip_decoder_init before use; clean up with
 * @ref slip_decoder_release.
 */
struct slip_decoder {
	struct sbuf frame; /**< Completed frame payload (valid after feed
				returns 1, until the next reset). */
	enum {
		SLIP_WAIT_START, /**< dropping noise until first 0xC0 */
		SLIP_IN_FRAME,	 /**< collecting data */
		SLIP_ESCAPING,	 /**< previous byte was 0xDB */
	} state;
};

/** Reset to the initial state; @c d->frame starts empty. */
void slip_decoder_init(struct slip_decoder *d);

/** Free any buffer owned by @c d->frame. */
void slip_decoder_release(struct slip_decoder *d);

/**
 * Clear the last decoded frame and prepare to collect another.
 * Safe to call in any state; equivalent to init() without releasing
 * the buffer.
 */
void slip_decoder_reset(struct slip_decoder *d);

/**
 * @brief Feed one incoming byte into the decoder.
 *
 * @return 1 if a complete frame is now available in @c d->frame,
 *         0 if more bytes are needed, -1 on a protocol error
 *         (an invalid escape after 0xDB).  On -1 the decoder resets
 *         itself to @c SLIP_WAIT_START so the next 0xC0 resyncs.
 */
int slip_feed(struct slip_decoder *d, uint8_t byte);

#endif /* SLIP_H */
