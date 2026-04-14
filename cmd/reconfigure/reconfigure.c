/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/reconfigure/reconfigure.c
 * @brief The "ice reconfigure" subcommand -- re-run cmake from scratch.
 */
#include "../../ice.h"

int cmd_reconfigure(int argc, const char **argv)
{
	const char *usage[] = { "ice reconfigure", NULL };
	struct option opts[] = { OPT_END() };

	parse_options(argc, argv, opts, usage);
	return ensure_build_directory(1);
}
