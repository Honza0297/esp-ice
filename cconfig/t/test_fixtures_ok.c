/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_fixtures_ok.c
 * @brief Fixture-driven tests for the cconfig processor.
 *
 * For each .in/.out fixture pair, parse the Kconfig, finalize,
 * generate sdkconfig output, and compare byte-for-byte against
 * the expected .out file.
 */
#include "cconfig/cconfig.h"
#include "ice.h"
#include "tap.h"

#include <stdio.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len)
{
	struct sbuf buf = SBUF_INIT;

	if (sbuf_read_file(&buf, path) < 0)
		return NULL;
	if (out_len)
		*out_len = buf.len;
	return sbuf_detach(&buf);
}

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
		die("usage: test_fixtures_ok <fixture_dir>");
	fixture_dir = argv[1];

	test_fixture(fixture_dir, "Empty");
	test_fixture(fixture_dir, "OneConfig");
	test_fixture(fixture_dir, "Comment");
	test_fixture(fixture_dir, "Choice");
	test_fixture(fixture_dir, "Menu");
	test_fixture(fixture_dir, "If");
	test_fixture(fixture_dir, "SelectImply");
	test_fixture(fixture_dir, "Version");
	test_fixture(fixture_dir, "ChoiceWithIf");
	test_fixture(fixture_dir, "EmptySourced");
	test_fixture(fixture_dir, "EnvironmentVariable");
	test_fixture(fixture_dir, "Expressions");
	test_fixture(fixture_dir, "Help");
	test_fixture(fixture_dir, "IfInsideChoice");
	test_fixture(fixture_dir, "IndirectValueSetComplex");
	test_fixture(fixture_dir, "IndirectValueSetSimple");
	test_fixture(fixture_dir, "Macro");
	test_fixture(fixture_dir, "MenuInsideChoice");
	test_fixture(fixture_dir, "MultipleDefinitionWithIgnore");
	test_fixture(fixture_dir, "Prompt");
	test_fixture(fixture_dir, "SetMultipleSources");
	test_fixture(fixture_dir, "SetWithMenuconfig");
	test_fixture(fixture_dir, "SeveralConfigs");
	test_fixture(fixture_dir, "Source");
	test_fixture(fixture_dir, "UndefinedQuotedMacro");
	test_fixture(fixture_dir, "Version_float");
	test_fixture(fixture_dir, "Warning");
	test_fixture(fixture_dir, "QuotedStringOrphan");
	test_fixture(fixture_dir, "OptionEnvUnset");
	test_fixture(fixture_dir, "OptionUnknown");

	return tap_result();
}
