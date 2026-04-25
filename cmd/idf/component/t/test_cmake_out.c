/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for cmd/idf/component/cmake_out.c.
 *
 * Checks that the emitted @c managed_components_list.temp.cmake and
 * @c components_with_manifests_list.temp files match the exact byte
 * layout produced by the Python @c idf_component_manager for the
 * scenarios we care about.
 */
#include "cmd/idf/component/cmake_out.h"
#include "ice.h"
#include "tap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path)
{
	struct sbuf sb = SBUF_INIT;
	if (sbuf_read_file(&sb, path) < 0) {
		sbuf_release(&sb);
		return NULL;
	}
	return sbuf_detach(&sb);
}

int main(void)
{
	/* Empty case: no locals with versions, no downloads.  Still writes
	 * the trailing @c set(managed_components "") so CMake's @c include
	 * does not leave @c managed_components undefined. */
	{
		const char *p = "empty.cmake";
		tap_check(cmake_out_emit_managed_list(p, NULL, 0, NULL, 0) ==
			  0);
		char *c = slurp(p);
		tap_check(c && !strcmp(c, "set(managed_components \"\")\n"));
		free(c);
		tap_done("emit empty managed list");
	}

	/* Only local components with versions: just the COMPONENT_VERSION
	 * lines and an empty list at the end. */
	{
		const char *p = "local.cmake";
		struct cmake_local_component locals[] = {
		    {"main", "1.0.0"},
		    {"helper", NULL}, /* skipped: no version */
		    {"logger", "0.2.3"},
		};
		tap_check(cmake_out_emit_managed_list(p, locals, 3, NULL, 0) ==
			  0);
		char *c = slurp(p);
		const char *expected = "idf_component_set_property(main "
				       "COMPONENT_VERSION \"1.0.0\")\n"
				       "idf_component_set_property(logger "
				       "COMPONENT_VERSION \"0.2.3\")\n"
				       "set(managed_components \"\")\n";
		tap_check(c && !strcmp(c, expected));
		free(c);
		tap_done("emit locals-only list");
	}

	/* Full case: mix of locals + downloads with targets, sorted by
	 * download-name regardless of input order. */
	{
		const char *p = "full.cmake";
		struct cmake_local_component locals[] = {
		    {"main", "1.0.0"},
		};
		const char *cjson_targets[] = {"esp32", "esp32s3"};
		struct cmake_dl_component dls[] = {
		    {
			.name = "espressif/led_strip",
			.abs_path =
			    "/proj/managed_components/espressif__led_strip",
			.version = "2.4.1",
		    },
		    {
			.name = "espressif/cjson",
			.abs_path = "/proj/managed_components/espressif__cjson",
			.version = "1.7.14",
			.targets = cjson_targets,
			.targets_nr = 2,
		    },
		};
		tap_check(cmake_out_emit_managed_list(p, locals, 1, dls, 2) ==
			  0);
		char *c = slurp(p);
		/* Downloads are name-sorted: cjson before led_strip. */
		const char *expected =
		    "idf_component_set_property(main "
		    "COMPONENT_VERSION \"1.0.0\")\n"

		    "idf_build_component("
		    "\"/proj/managed_components/espressif__cjson\" "
		    "\"project_managed_components\")\n"
		    "idf_component_set_property(espressif/cjson "
		    "COMPONENT_VERSION \"1.7.14\")\n"
		    "idf_component_set_property(espressif/cjson "
		    "REQUIRED_IDF_TARGETS \"esp32 esp32s3\")\n"

		    "idf_build_component("
		    "\"/proj/managed_components/espressif__led_strip\" "
		    "\"project_managed_components\")\n"
		    "idf_component_set_property(espressif/led_strip "
		    "COMPONENT_VERSION \"2.4.1\")\n"

		    "set(managed_components "
		    "\"espressif/cjson;espressif/led_strip\")\n";
		tap_check(c && !strcmp(c, expected));
		free(c);
		tap_done("emit full list: locals + sorted downloads");
	}

	/* components_with_manifests_list.temp: newline-separated, sorted,
	 * no trailing newline (matches the Python '\n'.join shape). */
	{
		const char *p = "paths.txt";
		const char *paths[] = {
		    "/proj/main",
		    "/proj/components/helper",
		    "/proj/managed_components/espressif__cjson",
		};
		tap_check(cmake_out_emit_components_paths(p, paths, 3) == 0);
		char *c = slurp(p);
		const char *expected =
		    "/proj/components/helper\n"
		    "/proj/main\n"
		    "/proj/managed_components/espressif__cjson";
		tap_check(c && !strcmp(c, expected));
		free(c);
		tap_done("emit components-with-manifests paths");
	}

	/* Paths empty case: file exists but is zero bytes. */
	{
		const char *p = "empty_paths.txt";
		tap_check(cmake_out_emit_components_paths(p, NULL, 0) == 0);
		char *c = slurp(p);
		tap_check(c && *c == '\0');
		free(c);
		tap_done("emit empty paths list");
	}

	return tap_result();
}
