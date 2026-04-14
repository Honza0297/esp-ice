/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/cmake/cmake.c
 * @brief The "ice cmake" subcommand -- invoke an arbitrary cmake target.
 *
 * Escape hatch for targets the higher-level wrappers don't cover
 * (size, partition-table, bootloader, app, erase-flash, ...).  The
 * target name is passed through verbatim; any unknown target is
 * rejected by cmake itself.
 *
 * Runs with stdio attached so the target's output (size reports,
 * menuconfig TUI, etc.) is visible directly.  Use "ice build" if you
 * want the captured progress display for a full build.
 */
#include "../../ice.h"

int cmd_cmake(int argc, const char **argv)
{
	const char *usage[] = { "ice cmake <target>", NULL };
	struct option opts[] = { OPT_END() };

	argc = parse_options(argc, argv, opts, usage);

	if (argc < 1)
		die("missing target argument");
	if (argc > 1)
		die("too many arguments");

	return run_cmake_target(argv[0], argv[0], 1);
}
