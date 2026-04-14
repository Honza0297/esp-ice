#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#
# Compile and run the C unit tests for json.

O="$T_OUT/$(basename "$0" .t)"
rm -rf "$O" && mkdir -p "$O"

# Pick the platform sources matching what the build targets.  When
# cross-compiling Windows binaries from a non-Windows host the
# resulting .exe cannot run here -- skip cleanly in that case.  Native
# MSYS2 / MinGW / Cygwin (S=win on a Windows host) proceeds normally.
case "$S" in
win)
	case "$(uname -s 2>/dev/null)" in
	MINGW*|MSYS*|CYGWIN*) ;;
	*)
		echo "1..0 # SKIP S=win binary cannot run on $(uname -s)"
		exit 0
		;;
	esac
	PLATFORM="platform/win/io.c platform/win/wconv.c"
	;;
*)
	PLATFORM="platform/posix/posix_io.c"
	;;
esac

DEPS="json.c sbuf.c error.c term.c pager.c $PLATFORM"
$CC -std=c99 -I. -It -o "$O/test_json" t/test_json.c $DEPS || exit 1
cd "$O" && ./test_json
