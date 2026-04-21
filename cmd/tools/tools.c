/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/tools/tools.c
 * @brief `ice tools` -- manage ESP-IDF toolchains (namespace dispatcher).
 */
#include "ice.h"

extern const struct cmd_desc cmd_tools_install_desc;
extern const struct cmd_desc cmd_tools_list_desc;
extern const struct cmd_desc cmd_tools_info_desc;

static const struct option cmd_tools_opts[] = {OPT_END()};

/* clang-format off */
static const struct cmd_manual tools_manual = {
	.name = "ice tools",
	.summary = "manage ESP-IDF toolchains",

	.description =
	H_PARA("Manage ESP-IDF toolchains (compilers, debuggers, etc.).")
	H_PARA("Run @b{ice tools <subcommand> --help} for details."),

	.examples =
	H_EXAMPLE("ice tools install ~/work/esp-idf/tools/tools.json")
	H_EXAMPLE("ice tools install --target esp32s3 tools/tools.json")
	H_EXAMPLE("ice tools list")
	H_EXAMPLE("ice tools info"),
};
/* clang-format on */

static const struct cmd_desc *const tools_subs[] = {
    &cmd_tools_install_desc,
    &cmd_tools_list_desc,
    &cmd_tools_info_desc,
    NULL,
};

const struct cmd_desc cmd_tools_desc = {
    .name = "tools",
    .opts = cmd_tools_opts,
    .manual = &tools_manual,
    .subcommands = tools_subs,
};
