#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#
# Compile and run the C unit tests for sbuf.

O="$T_OUT/$(basename "$0" .t)"
rm -rf "$O" && mkdir -p "$O"

# Windows would need its own platform sources and a -municode-aware
# entry point; out of scope for v1.
case "$S" in
win)
	echo "1..0 # SKIP C unit tests are POSIX-only for now"
	exit 0
	;;
esac

DEPS="sbuf.c error.c term.c pager.c platform/posix/posix_io.c"
$CC -std=c99 -I. -It -o "$O/test_sbuf" t/test_sbuf.c $DEPS || exit 1
cd "$O" && ./test_sbuf
