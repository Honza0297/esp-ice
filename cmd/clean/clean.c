/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/clean/clean.c
 * @brief The "ice clean" subcommand -- invoke the cmake "clean" target.
 */
#include "../../ice.h"

int cmd_clean(int argc, const char **argv)
{
	const char *usage[] = { "ice clean", NULL };
	struct option opts[] = { OPT_END() };

	parse_options(argc, argv, opts, usage);
	return run_cmake_target("clean", "clean", 0);
}
