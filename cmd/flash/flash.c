/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/flash/flash.c
 * @brief The "ice flash" subcommand -- invoke the cmake "flash" target.
 */
#include "../../ice.h"

int cmd_flash(int argc, const char **argv)
{
	const char *usage[] = { "ice flash", NULL };
	struct option opts[] = { OPT_END() };

	parse_options(argc, argv, opts, usage);
	return run_cmake_target("flash", "flash", 0);
}
