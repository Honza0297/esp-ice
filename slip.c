/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file slip.c
 * @brief SLIP framer implementation -- see @ref slip.h.
 */
#include "slip.h"
#include "ice.h"

void slip_encode(struct sbuf *out, const void *data, size_t n)
{
	const uint8_t *p = (const uint8_t *)data;

	sbuf_addch(out, (int)SLIP_END);
	for (size_t i = 0; i < n; i++) {
		if (p[i] == SLIP_END) {
			sbuf_addch(out, (int)SLIP_ESC);
			sbuf_addch(out, (int)SLIP_ESC_END);
		} else if (p[i] == SLIP_ESC) {
			sbuf_addch(out, (int)SLIP_ESC);
			sbuf_addch(out, (int)SLIP_ESC_ESC);
		} else {
			sbuf_addch(out, (int)p[i]);
		}
	}
	sbuf_addch(out, (int)SLIP_END);
}

void slip_decoder_init(struct slip_decoder *d)
{
	sbuf_init(&d->frame);
	d->state = SLIP_WAIT_START;
}

void slip_decoder_release(struct slip_decoder *d)
{
	sbuf_release(&d->frame);
	d->state = SLIP_WAIT_START;
}

void slip_decoder_reset(struct slip_decoder *d)
{
	sbuf_reset(&d->frame);
	d->state = SLIP_WAIT_START;
}

int slip_feed(struct slip_decoder *d, uint8_t byte)
{
	switch (d->state) {
	case SLIP_WAIT_START:
		if (byte == SLIP_END) {
			/* Opening delimiter -- start collecting.  We
			 * stay in IN_FRAME on the next 0xC0 so that
			 * two delimiter bytes back-to-back decode as
			 * an empty frame plus the start of a new one. */
			sbuf_reset(&d->frame);
			d->state = SLIP_IN_FRAME;
		}
		/* else: pre-frame noise, drop silently */
		return 0;

	case SLIP_IN_FRAME:
		if (byte == SLIP_END) {
			/* Closing delimiter -- frame ready.  Stay in
			 * IN_FRAME so the same 0xC0 also acts as the
			 * opening delimiter of the next frame. */
			return 1;
		}
		if (byte == SLIP_ESC) {
			d->state = SLIP_ESCAPING;
			return 0;
		}
		sbuf_addch(&d->frame, (int)byte);
		return 0;

	case SLIP_ESCAPING:
		if (byte == SLIP_ESC_END) {
			sbuf_addch(&d->frame, (int)SLIP_END);
			d->state = SLIP_IN_FRAME;
			return 0;
		}
		if (byte == SLIP_ESC_ESC) {
			sbuf_addch(&d->frame, (int)SLIP_ESC);
			d->state = SLIP_IN_FRAME;
			return 0;
		}
		/* Invalid escape -- reset and signal error; next 0xC0
		 * will resync. */
		sbuf_reset(&d->frame);
		d->state = SLIP_WAIT_START;
		return -1;
	}
	/* unreachable */
	return -1;
}
