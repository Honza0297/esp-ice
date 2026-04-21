/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/tools/info/info.c
 * @brief `ice tools info` -- summarise tools path and delegate to list.
 */
#include "ice.h"

extern int cmd_tools_list(int argc, const char **argv);

static const struct cmd_manual tools_info_manual = {.name = "ice tools info"};
static const struct option cmd_tools_info_opts[] = {OPT_END()};

int cmd_tools_info(int argc, const char **argv);

const struct cmd_desc cmd_tools_info_desc = {
    .name = "info",
    .fn = cmd_tools_info,
    .opts = cmd_tools_info_opts,
    .manual = &tools_info_manual,
};

int cmd_tools_info(int argc, const char **argv)
{
	parse_options(argc, argv, &cmd_tools_info_desc);

	printf("Tools path: %s\n", ice_home());
	printf("Platform:   %s-%s\n", ICE_PLATFORM_OS, ICE_PLATFORM_ARCH);
	return cmd_tools_list(argc, argv);
}
