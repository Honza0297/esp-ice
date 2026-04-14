/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for svec.c -- the NULL-terminated string vector.
 */
#include "ice.h"
#include "tap.h"

int main(void)
{
	/* SVEC_INIT yields an empty NULL-terminated vector. */
	{
		struct svec v = SVEC_INIT;
		tap_check(v.v != NULL);
		tap_check(v.nr == 0);
		tap_check(v.alloc == 0);
		tap_check(v.v[0] == NULL);
		svec_clear(&v);
		tap_done("SVEC_INIT yields empty NULL-terminated vector");
	}

	/* svec_push appends and keeps the NULL terminator after the last entry.
	 */
	{
		struct svec v = SVEC_INIT;
		svec_push(&v, "one");
		svec_push(&v, "two");
		svec_push(&v, "three");
		tap_check(v.nr == 3);
		tap_check(strcmp(v.v[0], "one") == 0);
		tap_check(strcmp(v.v[1], "two") == 0);
		tap_check(strcmp(v.v[2], "three") == 0);
		tap_check(v.v[3] == NULL);
		svec_clear(&v);
		tap_done("svec_push appends and keeps NULL terminator");
	}

	/* Strings are copied -- mutating the source after push does not affect
	 * what the vector returns. */
	{
		struct svec v = SVEC_INIT;
		char src[] = "original";
		svec_push(&v, src);
		src[0] = 'X';
		tap_check(strcmp(v.v[0], "original") == 0);
		svec_clear(&v);
		tap_done("svec_push copies the string, not the pointer");
	}

	/* svec_pushf supports printf-style formatting. */
	{
		struct svec v = SVEC_INIT;
		svec_pushf(&v, "%s-%d", "key", 7);
		tap_check(strcmp(v.v[0], "key-7") == 0);
		svec_clear(&v);
		tap_done("svec_pushf formats printf-style");
	}

	/* svec_pop removes and frees the last entry. */
	{
		struct svec v = SVEC_INIT;
		svec_push(&v, "a");
		svec_push(&v, "b");
		svec_pop(&v);
		tap_check(v.nr == 1);
		tap_check(strcmp(v.v[0], "a") == 0);
		tap_check(v.v[1] == NULL);
		svec_clear(&v);
		tap_done("svec_pop removes the last entry");
	}

	/* svec_clear resets to the empty sentinel. */
	{
		struct svec v = SVEC_INIT;
		svec_push(&v, "anything");
		svec_clear(&v);
		tap_check(v.nr == 0);
		tap_check(v.alloc == 0);
		tap_check(v.v == svec_empty);
		tap_done("svec_clear returns to empty sentinel");
	}

	return tap_result();
}
