/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/tools/list/list.c
 * @brief `ice tools list` -- show installed tool versions.
 */
#include "ice.h"

struct list_version_ctx {
	const char *tool_name;
	const char *tool_dir;
};

static int list_version_cb(const char *version, void *ud)
{
	struct list_version_ctx *ctx = ud;
	struct sbuf path = SBUF_INIT;

	sbuf_addf(&path, "%s/%s", ctx->tool_dir, version);
	if (is_directory(path.buf))
		printf("  %-30s %s\n", ctx->tool_name, version);
	sbuf_release(&path);
	return 0;
}

static int list_tool_cb(const char *name, void *ud)
{
	const char *tools_dir = ud;
	struct sbuf path = SBUF_INIT;
	struct list_version_ctx ctx;

	sbuf_addf(&path, "%s/%s", tools_dir, name);
	if (!is_directory(path.buf)) {
		sbuf_release(&path);
		return 0;
	}

	ctx.tool_name = name;
	ctx.tool_dir = path.buf;
	dir_foreach(path.buf, list_version_cb, &ctx);
	sbuf_release(&path);
	return 0;
}

static const struct cmd_manual tools_list_manual = {.name = "ice tools list"};
static const struct option cmd_tools_list_opts[] = {OPT_END()};

int cmd_tools_list(int argc, const char **argv);

const struct cmd_desc cmd_tools_list_desc = {
    .name = "list",
    .fn = cmd_tools_list,
    .opts = cmd_tools_list_opts,
    .manual = &tools_list_manual,
};

int cmd_tools_list(int argc, const char **argv)
{
	struct sbuf tools_dir = SBUF_INIT;

	parse_options(argc, argv, &cmd_tools_list_desc);

	sbuf_addf(&tools_dir, "%s/tools", ice_home());

	if (access(tools_dir.buf, F_OK) != 0) {
		fprintf(stderr, "No tools installed.\n");
		hint("run @b{ice tools install <tools.json>}");
		sbuf_release(&tools_dir);
		return 1;
	}

	dir_foreach(tools_dir.buf, list_tool_cb, (void *)tools_dir.buf);
	sbuf_release(&tools_dir);
	return 0;
}
