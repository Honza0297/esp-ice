/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file target.c
 * @brief `ice target` -- query the chip target.
 *
 * Subcommands:
 *   list  - list supported chip targets
 *   info  - show the project's currently-configured target
 *
 * Setting the target is done by `ice init <chip> <idf>`; this
 * namespace is read-only.
 */
#include "ice.h"

/*
 * Mirrors esp-idf/tools/idf_py_actions/constants.py.  Preview targets
 * require --preview to enable.  Exposed via ice.h so callers (init,
 * install, completion) can reuse the same lists.
 */
const char *const ice_supported_targets[] = {
    "esp32",   "esp32s2", "esp32c3", "esp32s3",	 "esp32c2", "esp32c6",
    "esp32h2", "esp32p4", "esp32c5", "esp32c61", NULL,
};
const char *const ice_preview_targets[] = {
    "linux", "esp32h21", "esp32h4", "esp32s31", NULL,
};

/* ------------------------------------------------------------------ */
/* ice target list                                                     */
/* ------------------------------------------------------------------ */

static int cmd_target_list(int argc, const char **argv);

static const struct cmd_manual target_list_manual = {
    .name = "ice target list",
};

static const struct option cmd_target_list_opts[] = {OPT_END()};

static const struct cmd_desc cmd_target_list_desc = {
    .name = "list",
    .fn = cmd_target_list,
    .opts = cmd_target_list_opts,
    .manual = &target_list_manual,
};

static int cmd_target_list(int argc, const char **argv)
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

/* ------------------------------------------------------------------ */
/* ice target info                                                     */
/* ------------------------------------------------------------------ */

static int cmd_target_info(int argc, const char **argv);

static const struct cmd_manual target_info_manual = {
    .name = "ice target info",
};

static const struct option cmd_target_info_opts[] = {OPT_END()};

static const struct cmd_desc cmd_target_info_desc = {
    .name = "info",
    .fn = cmd_target_info,
    .opts = cmd_target_info_opts,
    .manual = &target_info_manual,
};

static int cmd_target_info(int argc, const char **argv)
{
	const char *target;

	parse_options(argc, argv, &cmd_target_info_desc);

	target = config_get("project.default.chip");
	if (!target || !*target) {
		fprintf(stderr, "No target set.\n"
				"hint: run @b{ice init <chip> <idf>}\n");
		return 1;
	}

	printf("Target: %s\n", target);
	return 0;
}

/* ------------------------------------------------------------------ */
/* ice target -- namespace dispatcher                                  */
/* ------------------------------------------------------------------ */

static const struct option cmd_target_opts[] = {OPT_END()};

/* clang-format off */
static const struct cmd_manual target_manual = {
	.name = "ice target",
	.summary = "query the chip target",

	.description =
	H_PARA("Inspect chip-target state for the current project.")
	H_PARA("Setting the target is done by @b{ice init <chip> <idf>}; "
	       "this namespace is read-only.")
	H_PARA("Run @b{ice target <subcommand> --help} for details."),

	.examples =
	H_EXAMPLE("ice target list")
	H_EXAMPLE("ice target info"),
};
/* clang-format on */

static const struct cmd_desc *const target_subs[] = {
    &cmd_target_list_desc,
    &cmd_target_info_desc,
    NULL,
};

const struct cmd_desc cmd_target_desc = {
    .name = "target",
    .opts = cmd_target_opts,
    .manual = &target_manual,
    .subcommands = target_subs,
};
