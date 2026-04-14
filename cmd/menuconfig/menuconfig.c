/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/menuconfig/menuconfig.c
 * @brief The "ice menuconfig" subcommand -- invoke the cmake "menuconfig" target.
 */
#include "../../ice.h"

int cmd_menuconfig(int argc, const char **argv)
{
	const char *usage[] = { "ice menuconfig", NULL };
	struct option opts[] = { OPT_END() };

	parse_options(argc, argv, opts, usage);
	return run_cmake_target("menuconfig", "menuconfig", 1);
}
