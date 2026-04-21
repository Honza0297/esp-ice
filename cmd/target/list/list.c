/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/target/list/list.c
 * @brief `ice target list` -- enumerate supported and preview chips.
 */
#include "ice.h"

static const struct cmd_manual target_list_manual = {
    .name = "ice target list",
};

static const struct option cmd_target_list_opts[] = {OPT_END()};

int cmd_target_list(int argc, const char **argv);

const struct cmd_desc cmd_target_list_desc = {
    .name = "list",
    .fn = cmd_target_list,
    .opts = cmd_target_list_opts,
    .manual = &target_list_manual,
};

int cmd_target_list(int argc, const char **argv)
{
	parse_options(argc, argv, &cmd_target_list_desc);

	printf("Supported targets:\n");
	for (const char *const *t = ice_supported_targets; *t; t++)
		printf("  %s\n", *t);

	printf("\nPreview targets (use 'ice init --preview' to enable):\n");
	for (const char *const *t = ice_preview_targets; *t; t++)
		printf("  %s\n", *t);

	return 0;
}
