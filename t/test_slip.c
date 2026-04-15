/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for slip.c.  Pure logic -- no I/O, no hardware, no
 * platform specifics.  Covers encode shape, byte-at-a-time decode,
 * escape-pair handling in both directions, frame boundaries
 * (including the ambiguous C0-between-frames case), and the error
 * path for an invalid escape.
 */
#include "ice.h"
#include "slip.h"
#include "tap.h"

static int feed_all(struct slip_decoder *d, const uint8_t *bytes, size_t n,
		    int *frames_out)
{
	int frames = 0;

	for (size_t i = 0; i < n; i++) {
		int r = slip_feed(d, bytes[i]);

		if (r < 0)
			return r;
		if (r == 1) {
			frames++;
			/* Leave the frame in d->frame for the caller to
			 * inspect when the helper returns, but reset after
			 * so subsequent bytes start a fresh frame. */
			if (i + 1 < n)
				slip_decoder_reset(d);
		}
	}
	*frames_out = frames;
	return 0;
}

int main(void)
{
	struct sbuf out = SBUF_INIT;
	struct slip_decoder d;

	/* --- Encoder --- */

	slip_encode(&out, NULL, 0);
	tap_check(out.len == 2);
	tap_check((uint8_t)out.buf[0] == 0xC0);
	tap_check((uint8_t)out.buf[1] == 0xC0);
	tap_done("encode empty frame is two 0xC0 bytes");

	sbuf_reset(&out);
	uint8_t one = 0xAA;
	slip_encode(&out, &one, 1);
	tap_check(out.len == 3);
	tap_check((uint8_t)out.buf[0] == 0xC0);
	tap_check((uint8_t)out.buf[1] == 0xAA);
	tap_check((uint8_t)out.buf[2] == 0xC0);
	tap_done("encode single unescaped byte");

	sbuf_reset(&out);
	uint8_t end_byte = 0xC0;
	slip_encode(&out, &end_byte, 1);
	tap_check(out.len == 4);
	tap_check((uint8_t)out.buf[0] == 0xC0);
	tap_check((uint8_t)out.buf[1] == 0xDB);
	tap_check((uint8_t)out.buf[2] == 0xDC);
	tap_check((uint8_t)out.buf[3] == 0xC0);
	tap_done("encode 0xC0 expands to 0xDB 0xDC");

	sbuf_reset(&out);
	uint8_t esc_byte = 0xDB;
	slip_encode(&out, &esc_byte, 1);
	tap_check(out.len == 4);
	tap_check((uint8_t)out.buf[1] == 0xDB);
	tap_check((uint8_t)out.buf[2] == 0xDD);
	tap_done("encode 0xDB expands to 0xDB 0xDD");

	sbuf_reset(&out);
	uint8_t mix[] = {0x01, 0xC0, 0x02, 0xDB, 0x03};
	slip_encode(&out, mix, sizeof mix);
	/* Expected: C0 01 DB DC 02 DB DD 03 C0 */
	static const uint8_t expected_mix[] = {
	    0xC0, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0,
	};
	tap_check(out.len == sizeof expected_mix);
	tap_check(memcmp(out.buf, expected_mix, sizeof expected_mix) == 0);
	tap_done("encode mixed payload escapes both 0xC0 and 0xDB");

	/* --- Decoder --- */

	slip_decoder_init(&d);

	/* C0 AA C0 -> [AA] */
	static const uint8_t f1[] = {0xC0, 0xAA, 0xC0};
	int frames;
	tap_check(feed_all(&d, f1, sizeof f1, &frames) == 0);
	tap_check(frames == 1);
	tap_check(d.frame.len == 1);
	tap_check((uint8_t)d.frame.buf[0] == 0xAA);
	slip_decoder_reset(&d);
	tap_done("decode single unescaped byte");

	/* C0 DB DC C0 -> [C0] */
	static const uint8_t f2[] = {0xC0, 0xDB, 0xDC, 0xC0};
	tap_check(feed_all(&d, f2, sizeof f2, &frames) == 0);
	tap_check(frames == 1);
	tap_check(d.frame.len == 1);
	tap_check((uint8_t)d.frame.buf[0] == 0xC0);
	slip_decoder_reset(&d);
	tap_done("decode 0xDB 0xDC -> 0xC0");

	/* C0 DB DD C0 -> [DB] */
	static const uint8_t f3[] = {0xC0, 0xDB, 0xDD, 0xC0};
	tap_check(feed_all(&d, f3, sizeof f3, &frames) == 0);
	tap_check(frames == 1);
	tap_check(d.frame.len == 1);
	tap_check((uint8_t)d.frame.buf[0] == 0xDB);
	slip_decoder_reset(&d);
	tap_done("decode 0xDB 0xDD -> 0xDB");

	/* Full roundtrip of the "mix" payload. */
	sbuf_reset(&out);
	slip_encode(&out, mix, sizeof mix);
	slip_decoder_reset(&d);
	tap_check(feed_all(&d, (const uint8_t *)out.buf, out.len, &frames) ==
		  0);
	tap_check(frames == 1);
	tap_check(d.frame.len == sizeof mix);
	tap_check(memcmp(d.frame.buf, mix, sizeof mix) == 0);
	slip_decoder_reset(&d);
	tap_done("encode -> decode round-trips a mixed payload");

	/* Two back-to-back frames share their boundary 0xC0.
	 * Input bytes: C0 01 C0 02 C0   -> frames [01] and [02]. */
	static const uint8_t f_double[] = {0xC0, 0x01, 0xC0, 0x02, 0xC0};
	frames = 0;
	for (size_t i = 0; i < sizeof f_double; i++) {
		int r = slip_feed(&d, f_double[i]);

		tap_check(r >= 0);
		if (r == 1) {
			frames++;
			tap_check(d.frame.len == 1);
			tap_check((uint8_t)d.frame.buf[0] == frames);
			slip_decoder_reset(&d);
			/* After reset, re-send a 0xC0 to restart -- the
			 * closing delimiter of the previous frame also
			 * acts as the opening delimiter of the next.  In
			 * practice the stream supplies that byte; we
			 * already consumed it here, so simulate by
			 * setting state to IN_FRAME by feeding a fresh
			 * SLIP_END. */
			slip_feed(&d, 0xC0);
		}
	}
	tap_check(frames == 2);
	slip_decoder_reset(&d);
	tap_done("back-to-back frames share boundary 0xC0");

	/* Noise before first 0xC0 is dropped. */
	slip_decoder_reset(&d);
	tap_check(slip_feed(&d, 0x11) == 0); /* pre-frame noise */
	tap_check(slip_feed(&d, 0x22) == 0);
	tap_check(slip_feed(&d, 0xC0) == 0); /* start */
	tap_check(slip_feed(&d, 0x33) == 0);
	tap_check(slip_feed(&d, 0xC0) == 1); /* end */
	tap_check(d.frame.len == 1);
	tap_check((uint8_t)d.frame.buf[0] == 0x33);
	tap_done("pre-frame noise is discarded until first 0xC0");
	slip_decoder_reset(&d);

	/* Invalid escape: 0xDB not followed by 0xDC or 0xDD -> -1. */
	tap_check(slip_feed(&d, 0xC0) == 0);
	tap_check(slip_feed(&d, 0xDB) == 0);
	tap_check(slip_feed(&d, 0xFF) == -1);
	/* Decoder auto-resets to WAIT_START so the next 0xC0 resyncs. */
	tap_check(slip_feed(&d, 0xC0) == 0);
	tap_check(slip_feed(&d, 0x77) == 0);
	tap_check(slip_feed(&d, 0xC0) == 1);
	tap_check(d.frame.len == 1);
	tap_check((uint8_t)d.frame.buf[0] == 0x77);
	tap_done("invalid escape returns -1 and resyncs on next 0xC0");

	slip_decoder_release(&d);
	sbuf_release(&out);
	return tap_result();
}
