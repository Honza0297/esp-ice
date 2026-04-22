#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#
# Compile and run all ok/ fixture tests for the cconfig processor.

O="$T_OUT/$(basename "$0" .t)"
rm -rf "$O" && mkdir -p "$O"

# Cross-compiled Windows binary cannot run on a non-Windows host;
# native MSYS2 / MinGW / Cygwin (S=win on a Windows host) is fine.
if [ "$S" = "win" ]; then
	case "$(uname -s 2>/dev/null)" in
	MINGW* | MSYS* | CYGWIN*) ;;
	*)
		echo "1..0 # SKIP S=win binary cannot run on $(uname -s)"
		exit 0
		;;
	esac
fi

cp -r cconfig/t/fixtures "$O/fixtures"

$CC -std=c99 $SAN_FLAGS -I. -It -o "$O/test_fixtures_ok" \
    cconfig/t/test_fixtures_ok.c "$LIBICE" $LINK_LIBS || exit 1

# Environment variables required by specific fixtures:
#   EnvironmentVariable.in: ${MAX_NUMBER_OF_MOTORS} forces env lookup
#   SeveralConfigs.in:      option env="TEST_ENV_SET"
#   Source.in:              $TEST_FILE_PREFIX in source paths
export MAX_NUMBER_OF_MOTORS=4
export TEST_ENV_SET=y
unset TEST_ENV_UNSET 2>/dev/null
export TEST_FILE_PREFIX="fixtures/ok/kconfigs_for_sourcing"

cd "$O" && ./test_fixtures_ok fixtures/ok
