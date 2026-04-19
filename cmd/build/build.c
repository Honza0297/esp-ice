/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/build/build.c
 * @brief The "ice build" subcommand -- build the default "all" target.
 */
#include "ice.h"

/* clang-format off */
static const struct cmd_manual build_manual = {
	.name = "ice build",
	.summary = "build the default target",

	.description =
	H_PARA("Compiles the project's @b{all} target through cmake.  "
	       "Progress is shown on a single status line and the full "
	       "command output is captured to "
	       "@b{<build-dir>/log/build.log}, surfaced on failure or "
	       "when @b{-v} / @b{core.verbose} is set.")
	H_PARA("The project must already be initialised with @b{ice init "
	       "<chip> <idf>}.  cmake re-runs automatically when its own "
	       "tracked dependencies (top-level @b{CMakeLists.txt}, "
	       "included @b{*.cmake} files) change.  For a from-scratch "
	       "rebuild, re-run @b{ice init}."),

	.examples =
	H_EXAMPLE("ice build")
	H_EXAMPLE("ice -v build")
	H_EXAMPLE("ice -D IDF_TARGET=esp32s3 build"),

	.extras =
	H_SECTION("CONFIG")
	H_ITEM("core.build-dir",
	       "Build directory (default @b{build}).")
	H_ITEM("core.generator",
	       "CMake generator passed on first configure "
	       "(default @b{Ninja}).")
	H_ITEM("core.verbose",
	       "If true, show full command output instead of the "
	       "progress line.")
	H_ITEM("cmake.define",
	       "Repeatable @b{-D<key>=<value>} entries forwarded to "
	       "cmake.")

	H_SECTION("SEE ALSO")
	H_ITEM("ice init",
	       "Bind/rebind the project (also re-runs cmake from scratch).")
	H_ITEM("ice clean",
	       "Remove build artifacts without touching the cmake "
	       "configuration.")
	H_ITEM("ice cmake <target>",
	       "Run an arbitrary cmake target with direct stdio."),
};
/* clang-format on */

static const struct option cmd_build_opts[] = {OPT_END()};

const struct cmd_desc cmd_build_desc = {
    .name = "build",
    .fn = cmd_build,
    .opts = cmd_build_opts,
    .manual = &build_manual,
};

int cmd_build(int argc, const char **argv)
{
	parse_options(argc, argv, &cmd_build_desc);
	return run_cmake_target("all", "build", 0);
}
