/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_confwrite.c
 * @brief Unit tests for the sdkconfig writer (kc_write_config /
 * kc_config_contents).
 *
 * For each fixture pair (.in / .out), parse the Kconfig, finalize,
 * generate the sdkconfig output, and compare byte-for-byte against
 * the expected .out file.
 */
#include "cconfig/cconfig.h"
#include "ice.h"
#include "tap.h"

#include <stdio.h>
#include <string.h>

/**
 * Read an entire file into a malloc'd buffer, NUL-terminated.
 * Returns NULL on failure.  Caller must free().
 */
static char *read_file(const char *path, size_t *out_len)
{
	struct sbuf buf = SBUF_INIT;

	if (sbuf_read_file(&buf, path) < 0)
		return NULL;
	if (out_len)
		*out_len = buf.len;
	return sbuf_detach(&buf);
}

/**
 * Parse a .in fixture, finalize, generate config output, and compare
 * byte-for-byte against the .out fixture.
 */
static void test_fixture(const char *fixture_dir, const char *name)
{
	char in_path[512];
	char out_path[512];
	char *expected;
	size_t expected_len;
	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf actual = SBUF_INIT;

	snprintf(in_path, sizeof(in_path), "%s/%s.in", fixture_dir, name);
	snprintf(out_path, sizeof(out_path), "%s/%s.out", fixture_dir, name);

	expected = read_file(out_path, &expected_len);
	tap_check(expected != NULL);

	kc_symtab_init(&tab);
	root = kc_parse_file(in_path, &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_config_contents(&actual, root, &tab, NULL);

	if (actual.len != expected_len ||
	    memcmp(actual.buf, expected, expected_len) != 0) {
		printf("# --- expected (%zu bytes) ---\n", expected_len);
		printf("%s", expected);
		printf("# --- actual (%zu bytes) ---\n", actual.len);
		printf("%s", actual.buf);
		tap_check(0);
	}

	sbuf_release(&actual);
	free(expected);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done(name);
}

int main(int argc, const char **argv)
{
	const char *fixture_dir;

	if (argc < 2)
		die("usage: test_confwrite <fixture_dir>");
	fixture_dir = argv[1];

	test_fixture(fixture_dir, "Empty");
	test_fixture(fixture_dir, "OneConfig");
	test_fixture(fixture_dir, "Comment");
	test_fixture(fixture_dir, "Choice");
	test_fixture(fixture_dir, "Menu");
	test_fixture(fixture_dir, "If");
	test_fixture(fixture_dir, "SelectImply");
	test_fixture(fixture_dir, "Version");

	/* String escaping: backslash and double-quote survive. */
	{
		static const char kconf[] =
		    "mainmenu \"Escape\"\n"
		    "\n"
		    "config PATH\n"
		    "  string \"path\"\n"
		    "  default \"C:\\\\Users\\\\me\\\"quoted\\\"\"\n";

		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct sbuf actual = SBUF_INIT;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconf, "escape.kconfig", &tab);
		tap_check(root != NULL);

		kc_finalize(root, &tab);
		kc_config_contents(&actual, root, &tab, NULL);

		tap_check(
		    strstr(actual.buf,
			   "CONFIG_PATH=\"C:\\\\Users\\\\me\\\"quoted\\\"\"") !=
		    NULL);

		sbuf_release(&actual);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("string escaping of backslash and double-quote");
	}

	/* KC_SYM_CHANGED suppresses the "# default:" pragma. */
	{
		static const char kconf[] = "mainmenu \"Changed\"\n"
					    "\n"
					    "config MY_OPT\n"
					    "  bool \"my opt\"\n"
					    "  default n\n";

		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct sbuf actual = SBUF_INIT;
		char *sdkpath;
		FILE *fp;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconf, "changed.kconfig", &tab);
		tap_check(root != NULL);

		sdkpath = sbuf_strdup("sdkconfig_changed");
		fp = fopen(sdkpath, "w");
		fputs("CONFIG_MY_OPT=y\n", fp);
		fclose(fp);

		kc_load_config(sdkpath, &tab);
		kc_finalize(root, &tab);
		kc_config_contents(&actual, root, &tab, NULL);

		tap_check(strstr(actual.buf, "CONFIG_MY_OPT=y") != NULL);
		tap_check(strstr(actual.buf, "# default:") == NULL);

		remove(sdkpath);
		free(sdkpath);
		sbuf_release(&actual);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("KC_SYM_CHANGED suppresses default pragma");
	}

	/* IDF version appears in the banner. */
	{
		static const char kconf[] = "mainmenu \"Banner\"\n";

		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct sbuf actual = SBUF_INIT;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconf, "banner.kconfig", &tab);
		tap_check(root != NULL);

		kc_finalize(root, &tab);
		kc_config_contents(&actual, root, &tab, "v5.3");

		tap_check(
		    strstr(actual.buf, "ESP-IDF) v5.3 Project Configuration") !=
		    NULL);

		sbuf_release(&actual);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("IDF version in banner");
	}

	/* Verify kc_write_config round-trips through a file. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct sbuf from_mem = SBUF_INIT;
		char *from_file;
		size_t from_file_len;
		char in_path[512];
		int rc;

		snprintf(in_path, sizeof(in_path), "%s/OneConfig.in",
			 fixture_dir);

		kc_symtab_init(&tab);
		root = kc_parse_file(in_path, &tab);
		kc_finalize(root, &tab);

		kc_config_contents(&from_mem, root, &tab, NULL);
		rc = kc_write_config("sdkconfig_roundtrip", root, &tab, NULL);
		tap_check(rc == 0);

		from_file = read_file("sdkconfig_roundtrip", &from_file_len);
		tap_check(from_file != NULL);
		tap_check(from_file_len == from_mem.len);
		tap_check(memcmp(from_file, from_mem.buf, from_mem.len) == 0);

		free(from_file);
		sbuf_release(&from_mem);
		remove("sdkconfig_roundtrip");
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("kc_write_config round-trip");
	}

	return tap_result();
}
