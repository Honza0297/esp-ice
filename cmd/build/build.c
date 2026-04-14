/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/build/build.c
 * @brief The "ice build" subcommand -- build the default "all" target.
 */
#include "../../ice.h"

int cmd_build(int argc, const char **argv)
{
	const char *usage[] = { "ice build", NULL };
	struct option opts[] = { OPT_END() };

	parse_options(argc, argv, opts, usage);
	return run_cmake_target("all", "build", 0);
}
