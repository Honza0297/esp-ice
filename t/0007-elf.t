#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#
# Compile and run the C unit tests for elf.

O="$T_OUT/$(basename "$0" .t)"
rm -rf "$O" && mkdir -p "$O"

# MinGW $CC produces COFF object files, not ELF, so the parser has
# nothing real to chew on.  Skip the test when targeting Windows,
# regardless of whether we're running natively or cross-compiling.
if [ "$S" = "win" ]; then
	echo "1..0 # SKIP elf test requires an ELF-producing toolchain"
	exit 0
fi

# Compile a small translation unit to produce a realistic fixture
# that has .text, .data, and .bss sections.
cat >"$O/stub.c" <<'EOF'
int initialised = 42;
char big_array[1000];
int fn(int x) { return x + 1; }
EOF
$CC -c "$O/stub.c" -o "$O/stub.o" || exit 1

$CC -std=c99 $SAN_FLAGS -I. -It -o "$O/test_elf" t/test_elf.c "$LIBICE" || exit 1
cd "$O" && ./test_elf
