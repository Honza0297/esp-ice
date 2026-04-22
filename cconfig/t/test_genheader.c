/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_genheader.c
 * @brief Unit tests for the C header output (kc_write_header /
 * kc_header_contents).
 *
 * Tests each symbol type, banner formatting, string escaping,
 * and a fixture-based comparison against known Kconfig input.
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

static void test_banner_no_version(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "banner.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "/*\n") == out.buf);
	tap_check(strstr(out.buf,
			 " * Automatically generated file. DO NOT EDIT.\n") !=
		  NULL);
	tap_check(strstr(out.buf, "ESP-IDF)  Configuration Header") != NULL);
	tap_check(strstr(out.buf, "#pragma once\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("banner without IDF version");
}

static void test_banner_with_version(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "banner.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, "v5.3");

	tap_check(strstr(out.buf, "ESP-IDF) v5.3 Configuration Header") !=
		  NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("banner with IDF version");
}

static void test_bool_y(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "config MY_BOOL\n"
				    "  bool \"prompt\"\n"
				    "  default y\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "bool_y.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_MY_BOOL 1\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("bool y emits #define 1");
}

static void test_bool_n_omitted(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "config MY_BOOL\n"
				    "  bool \"prompt\"\n"
				    "  default n\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "bool_n.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "MY_BOOL") == NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("bool n is omitted from header");
}

static void test_int_value(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "config MY_INT\n"
				    "  int \"prompt\"\n"
				    "  default 42\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "int.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_MY_INT 42\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("int value emits #define");
}

static void test_hex_with_prefix(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "config MY_HEX\n"
				    "  hex \"prompt\"\n"
				    "  default 0xFF\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "hex.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_MY_HEX 0xff\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("hex value preserves 0x prefix");
}

static void test_hex_without_prefix(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "config MY_HEX\n"
				    "  hex \"prompt\"\n"
				    "  default FF\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "hex_no0x.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_MY_HEX 0xff\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("hex value without 0x gets prefix added");
}

static void test_string_value(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "config MY_STR\n"
				    "  string \"prompt\"\n"
				    "  default \"hello world\"\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "str.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_MY_STR \"hello world\"\n") !=
		  NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("string value emits quoted #define");
}

static void test_string_escaping(void)
{
	static const char kconf[] =
	    "mainmenu \"Test\"\n"
	    "\n"
	    "config PATH\n"
	    "  string \"prompt\"\n"
	    "  default \"C:\\\\Users\\\\me\\\"quoted\\\"\"\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "escape.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf,
			 "#define CONFIG_PATH "
			 "\"C:\\\\Users\\\\me\\\"quoted\\\"\"") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("string escaping of backslash and double-quote");
}

static void test_float_value(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "config MY_FLOAT\n"
				    "  float \"prompt\"\n"
				    "  default 3.14\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "float.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_MY_FLOAT 3.14\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("float value emits #define");
}

static void test_no_pragmas_or_sections(void)
{
	static const char kconf[] = "mainmenu \"Test\"\n"
				    "\n"
				    "menu \"Section\"\n"
				    "  config A\n"
				    "    bool \"a\"\n"
				    "    default y\n"
				    "endmenu\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "sections.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "# default:") == NULL);
	tap_check(strstr(out.buf, "# Section") == NULL);
	tap_check(strstr(out.buf, "# end of") == NULL);
	tap_check(strstr(out.buf, "is not set") == NULL);
	tap_check(strstr(out.buf, "#define CONFIG_A 1\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("no pragmas, sections, or footers in header");
}

static void test_fixture_oneconfig(const char *fixture_dir)
{
	char in_path[512];
	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	static const char expected[] =
	    "/*\n"
	    " * Automatically generated file. DO NOT EDIT.\n"
	    " * Espressif IoT Development Framework (ESP-IDF) "
	    " Configuration Header\n"
	    " */\n"
	    "#pragma once\n"
	    "#define CONFIG_ONLY_CONFIG 1\n";

	snprintf(in_path, sizeof(in_path), "%s/OneConfig.in", fixture_dir);

	kc_symtab_init(&tab);
	root = kc_parse_file(in_path, &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	if (out.len != strlen(expected) ||
	    memcmp(out.buf, expected, strlen(expected)) != 0) {
		printf("# --- expected (%zu bytes) ---\n", strlen(expected));
		printf("%s", expected);
		printf("# --- actual (%zu bytes) ---\n", out.len);
		printf("%s", out.buf);
		tap_check(0);
	}

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("fixture OneConfig header output");
}

static void test_write_header_roundtrip(const char *fixture_dir)
{
	char in_path[512];
	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf from_mem = SBUF_INIT;
	char *from_file;
	size_t from_file_len;
	int rc;

	snprintf(in_path, sizeof(in_path), "%s/OneConfig.in", fixture_dir);

	kc_symtab_init(&tab);
	root = kc_parse_file(in_path, &tab);
	kc_finalize(root, &tab);

	kc_header_contents(&from_mem, root, &tab, NULL);
	rc = kc_write_header("sdkconfig_h_roundtrip", root, &tab, NULL);
	tap_check(rc == 0);

	from_file = read_file("sdkconfig_h_roundtrip", &from_file_len);
	tap_check(from_file != NULL);
	tap_check(from_file_len == from_mem.len);
	tap_check(memcmp(from_file, from_mem.buf, from_mem.len) == 0);

	free(from_file);
	sbuf_release(&from_mem);
	remove("sdkconfig_h_roundtrip");
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("kc_write_header round-trip");
}

static void test_select_forces_promptless_bool_header(void)
{
	static const char kconf[] = "mainmenu \"Sel\"\n"
				    "\n"
				    "config DRIVER\n"
				    "  bool \"driver\"\n"
				    "  default y\n"
				    "\n"
				    "config TAG\n"
				    "  bool\n"
				    "  default y\n"
				    "  select DRIVER\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "select.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_TAG 1\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("select forces promptless bool y in header");
}

static void test_config_then_header_same_tree(void)
{
	static const char kconf[] = "mainmenu \"Both\"\n"
				    "\n"
				    "config X\n"
				    "  bool \"x\"\n"
				    "  default y\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf cfg = SBUF_INIT;
	struct sbuf hdr = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "both.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);

	kc_config_contents(&cfg, root, &tab, NULL);
	kc_header_contents(&hdr, root, &tab, NULL);

	tap_check(strstr(cfg.buf, "CONFIG_X=y") != NULL);
	tap_check(strstr(hdr.buf, "#define CONFIG_X 1\n") != NULL);

	sbuf_release(&hdr);
	sbuf_release(&cfg);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("kc_config_contents then kc_header_contents same tree");
}

static void test_empty_mainmenu_header_only_banner(void)
{
	static const char kconf[] = "mainmenu \"Empty\"\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "empty.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#pragma once\n") != NULL);
	tap_check(strstr(out.buf, "#define ") == NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("empty mainmenu header is banner plus pragma only");
}

static void test_mixed_types(void)
{
	static const char kconf[] = "mainmenu \"Mixed\"\n"
				    "\n"
				    "config ENABLED\n"
				    "  bool \"on\"\n"
				    "  default y\n"
				    "\n"
				    "config DISABLED\n"
				    "  bool \"off\"\n"
				    "  default n\n"
				    "\n"
				    "config COUNT\n"
				    "  int \"count\"\n"
				    "  default 10\n"
				    "\n"
				    "config ADDR\n"
				    "  hex \"addr\"\n"
				    "  default 0xDEAD\n"
				    "\n"
				    "config NAME\n"
				    "  string \"name\"\n"
				    "  default \"test\"\n"
				    "\n"
				    "config RATIO\n"
				    "  float \"ratio\"\n"
				    "  default 1.5\n";

	struct kc_symtab tab;
	struct kc_menu_node *root;
	struct sbuf out = SBUF_INIT;

	kc_symtab_init(&tab);
	root = kc_parse_buffer(kconf, "mixed.kconfig", &tab);
	tap_check(root != NULL);

	kc_finalize(root, &tab);
	kc_header_contents(&out, root, &tab, NULL);

	tap_check(strstr(out.buf, "#define CONFIG_ENABLED 1\n") != NULL);
	tap_check(strstr(out.buf, "DISABLED") == NULL);
	tap_check(strstr(out.buf, "#define CONFIG_COUNT 10\n") != NULL);
	tap_check(strstr(out.buf, "#define CONFIG_ADDR 0xdead\n") != NULL);
	tap_check(strstr(out.buf, "#define CONFIG_NAME \"test\"\n") != NULL);
	tap_check(strstr(out.buf, "#define CONFIG_RATIO 1.5\n") != NULL);

	sbuf_release(&out);
	kc_menu_free(root);
	kc_symtab_release(&tab);
	tap_done("mixed types: bool y/n, int, hex, string, float");
}

int main(int argc, const char **argv)
{
	const char *fixture_dir;

	if (argc < 2)
		die("usage: test_genheader <fixture_dir>");
	fixture_dir = argv[1];

	test_banner_no_version();
	test_banner_with_version();
	test_bool_y();
	test_bool_n_omitted();
	test_int_value();
	test_hex_with_prefix();
	test_hex_without_prefix();
	test_string_value();
	test_string_escaping();
	test_float_value();
	test_no_pragmas_or_sections();
	test_fixture_oneconfig(fixture_dir);
	test_write_header_roundtrip(fixture_dir);
	test_select_forces_promptless_bool_header();
	test_config_then_header_same_tree();
	test_empty_mainmenu_header_only_banner();
	test_mixed_types();

	return tap_result();
}
