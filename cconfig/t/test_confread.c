/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_confread.c
 * @brief Unit tests for the sdkconfig reader (kc_load_config).
 */
#include "cconfig/cconfig.h"
#include "ice.h"
#include "tap.h"

#include <stdio.h>
#include <string.h>

static const char kconfig_tree[] = "mainmenu \"Test\"\n"
				   "\n"
				   "config BOOL_OPT\n"
				   "  bool \"A bool option\"\n"
				   "  default n\n"
				   "\n"
				   "config INT_OPT\n"
				   "  int \"An int option\"\n"
				   "  default 0\n"
				   "\n"
				   "config HEX_OPT\n"
				   "  hex \"A hex option\"\n"
				   "  default 0x0\n"
				   "\n"
				   "config STR_OPT\n"
				   "  string \"A string option\"\n"
				   "  default \"\"\n"
				   "\n"
				   "config FLOAT_OPT\n"
				   "  float \"A float option\"\n"
				   "  default \"0.0\"\n"
				   "\n"
				   "config UNSET_BOOL\n"
				   "  bool \"Unset bool\"\n"
				   "  default y\n";

/**
 * Write a NUL-terminated string to a temporary file and return its
 * path.  The caller must free() the returned path.
 */
static char *write_sdkconfig(const char *content, const char *name)
{
	char *path;
	FILE *fp;

	path = sbuf_strdup(name);
	fp = fopen(path, "w");
	if (!fp)
		die("cannot create %s", path);
	fputs(content, fp);
	fclose(fp);
	return path;
}

int main(void)
{
	/* 1. Load basic sdkconfig with all value types. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("#\n"
				       "# ESP-IDF Configuration\n"
				       "#\n"
				       "CONFIG_BOOL_OPT=y\n"
				       "CONFIG_INT_OPT=42\n"
				       "CONFIG_HEX_OPT=0x2a\n"
				       "CONFIG_STR_OPT=\"hello world\"\n"
				       "CONFIG_FLOAT_OPT=1.5\n"
				       "# CONFIG_UNSET_BOOL is not set\n",
				       "sdkconfig_basic");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 0);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "BOOL_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "y") == 0);

		sym = kc_symtab_lookup(&tab, "INT_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "42") == 0);

		sym = kc_symtab_lookup(&tab, "HEX_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "0x2a") == 0);

		sym = kc_symtab_lookup(&tab, "STR_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "hello world") == 0);

		sym = kc_symtab_lookup(&tab, "FLOAT_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "1.5") == 0);

		sym = kc_symtab_lookup(&tab, "UNSET_BOOL");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "n") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("load basic sdkconfig with all value types");
	}

	/* 2. Unknown symbol in sdkconfig is warned and skipped. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_BOOL_OPT=y\n"
				       "CONFIG_DOES_NOT_EXIST=y\n",
				       "sdkconfig_unknown");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		tap_check(kc_symtab_lookup(&tab, "DOES_NOT_EXIST") == NULL);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("unknown symbol warned and skipped");
	}

	/* 3. Unknown symbol via '# CONFIG_X is not set' warned and skipped. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("# CONFIG_PHANTOM is not set\n",
				       "sdkconfig_unset_unknown");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		tap_check(kc_symtab_lookup(&tab, "PHANTOM") == NULL);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("unknown symbol via 'is not set' warned and skipped");
	}

	/* 4. Type mismatch: bool value on int symbol is warned. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path =
		    write_sdkconfig("CONFIG_INT_OPT=y\n", "sdkconfig_mismatch");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		sym = kc_symtab_lookup(&tab, "INT_OPT");
		tap_check(sym != NULL);
		tap_check(!(sym->flags & KC_SYM_CHANGED));

		kc_finalize(root, &tab);

		/* Value should remain the default since mismatch was skipped */
		tap_check(strcmp(sym->curr_value, "0") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("type mismatch: bool value on int symbol warned");
	}

	/* 5. 'is not set' on non-bool type is warned. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("# CONFIG_INT_OPT is not set\n",
				       "sdkconfig_unset_nonbool");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "INT_OPT");
		tap_check(sym != NULL);
		/* INT_OPT should keep default since unset on int is invalid */
		tap_check(strcmp(sym->curr_value, "0") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("'is not set' on non-bool type warned");
	}

	/* 6. Comments, blank lines, and banner header are skipped. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig(
		    "#\n"
		    "# Automatically generated file; DO NOT EDIT.\n"
		    "# ESP-IDF Configuration\n"
		    "#\n"
		    "\n"
		    "\n"
		    "# This is a comment\n"
		    "CONFIG_BOOL_OPT=y\n"
		    "\n"
		    "# Another comment\n",
		    "sdkconfig_comments");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 0);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "BOOL_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "y") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("comments, blank lines, and banner are skipped");
	}

	/* 7. Nonexistent file returns -1. */
	{
		struct kc_symtab tab;
		int rc;

		kc_symtab_init(&tab);
		rc = kc_load_config("no_such_sdkconfig_file", &tab);
		tap_check(rc == -1);

		kc_symtab_release(&tab);
		tap_done("nonexistent file returns -1");
	}

	/* 8. Loaded values override Kconfig defaults after finalize. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer("mainmenu \"T\"\n"
				       "config MY_BOOL\n"
				       "  bool\n"
				       "  default y\n"
				       "\n"
				       "config MY_INT\n"
				       "  int\n"
				       "  default 99\n",
				       "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_MY_BOOL=n\n"
				       "CONFIG_MY_INT=7\n",
				       "sdkconfig_override");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "MY_BOOL");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "n") == 0);

		sym = kc_symtab_lookup(&tab, "MY_INT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "7") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("loaded values override Kconfig defaults");
	}

	/* 9. Empty sdkconfig file is accepted without error. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("", "sdkconfig_empty");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("empty sdkconfig accepted without error");
	}

	/* 10. Hex type mismatch: decimal on hex symbol is warned. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_HEX_OPT=42\n",
				       "sdkconfig_hex_mismatch");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "HEX_OPT");
		tap_check(sym != NULL);
		/* Value should remain default since mismatch was skipped */
		tap_check(strcmp(sym->curr_value, "0x0") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("hex type mismatch: decimal on hex symbol warned");
	}

	/* 11. Empty value (CONFIG_STR_OPT=) stores empty string. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_STR_OPT=\n",
				       "sdkconfig_empty_value");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "STR_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("empty value stores empty string");
	}

	/* 12. Embedded '=' in quoted string value. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_STR_OPT=\"a=b\"\n",
				       "sdkconfig_embedded_eq");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "STR_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "a=b") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("embedded '=' in quoted string value preserved");
	}

	/* 13. Duplicate symbol: last value wins. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_INT_OPT=10\n"
				       "CONFIG_INT_OPT=20\n",
				       "sdkconfig_duplicate");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "INT_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "20") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("duplicate symbol: last value wins");
	}

	/* 14. Unquoted string on string type is accepted. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_STR_OPT=bare_value\n",
				       "sdkconfig_unquoted_str");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "STR_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "bare_value") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("unquoted string on string type accepted");
	}

	/* 15. Trailing whitespace is stripped before processing. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_BOOL_OPT=y   \n"
				       "# CONFIG_UNSET_BOOL is not set  \n",
				       "sdkconfig_trailing_ws");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "BOOL_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "y") == 0);

		sym = kc_symtab_lookup(&tab, "UNSET_BOOL");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "n") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("trailing whitespace stripped before processing");
	}

	/* 16. Trailing garbage after 'is not set' is rejected. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig(
		    "# CONFIG_UNSET_BOOL is not set garbage here\n",
		    "sdkconfig_trailing_garbage");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "UNSET_BOOL");
		tap_check(sym != NULL);
		/* Should keep default (y) since the line was rejected */
		tap_check(strcmp(sym->curr_value, "y") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("trailing garbage after 'is not set' rejected");
	}

	/* 17. KC_SYM_CHANGED flag is set on loaded symbols. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_BOOL_OPT=y\n"
				       "# CONFIG_UNSET_BOOL is not set\n",
				       "sdkconfig_changed_flag");

		kc_load_config(path, &tab);

		sym = kc_symtab_lookup(&tab, "BOOL_OPT");
		tap_check(sym != NULL);
		tap_check((sym->flags & KC_SYM_CHANGED) != 0);

		sym = kc_symtab_lookup(&tab, "UNSET_BOOL");
		tap_check(sym != NULL);
		tap_check((sym->flags & KC_SYM_CHANGED) != 0);

		/* Symbols not in sdkconfig should NOT have the flag */
		sym = kc_symtab_lookup(&tab, "INT_OPT");
		tap_check(sym != NULL);
		tap_check((sym->flags & KC_SYM_CHANGED) == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("KC_SYM_CHANGED flag set on loaded symbols");
	}

	/* 18. Range clamping: loaded value above max is clamped. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer("mainmenu \"T\"\n"
				       "config RANGED_INT\n"
				       "  int \"Ranged\"\n"
				       "  default 50\n"
				       "  range 0 100\n",
				       "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_RANGED_INT=1000\n",
				       "sdkconfig_range_clamp");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "RANGED_INT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "100") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("range clamping: loaded value above max is clamped");
	}

	/* 19. Loaded bool forced by select after finalize. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer("mainmenu \"T\"\n"
				       "config SEL_TRIGGER\n"
				       "  bool \"trigger\"\n"
				       "  default y\n"
				       "  select SEL_TARGET\n"
				       "\n"
				       "config SEL_TARGET\n"
				       "  bool \"target\"\n"
				       "  default y\n",
				       "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_SEL_TARGET=n\n",
				       "sdkconfig_select_force");

		kc_load_config(path, &tab);
		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "SEL_TARGET");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "y") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("loaded bool forced by select after finalize");
	}

	/* 20. Malformed lines: empty name and missing '=' warn. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_=y\n"
				       "CONFIG_FOO\n",
				       "sdkconfig_malformed");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 2);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("malformed lines: empty name and missing '=' warn");
	}

	/* 21. Unterminated quote produces a warning. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_STR_OPT=\"hello\n",
				       "sdkconfig_unterminated_quote");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "STR_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "\"hello") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("unterminated quote produces a warning");
	}

	/* 22. Empty int value is rejected. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path =
		    write_sdkconfig("CONFIG_INT_OPT=\n", "sdkconfig_empty_int");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "INT_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "0") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("empty int value is rejected");
	}

	/* 23. '# CONFIG_X is not set' on string symbol is rejected. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;
		int rc;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("# CONFIG_STR_OPT is not set\n",
				       "sdkconfig_unset_string");

		rc = kc_load_config(path, &tab);
		tap_check(rc == 1);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "STR_OPT");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->curr_value, "") == 0);

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("'is not set' on string symbol rejected");
	}

	/* 24. KC_SYM_CHANGED NOT set on all rejection types. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		char *path;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(kconfig_tree, "test.kconfig", &tab);

		path = write_sdkconfig("CONFIG_INT_OPT=y\n"
				       "CONFIG_HEX_OPT=42\n"
				       "# CONFIG_STR_OPT is not set\n",
				       "sdkconfig_rejected_flags");

		kc_load_config(path, &tab);

		sym = kc_symtab_lookup(&tab, "INT_OPT");
		tap_check(sym != NULL);
		tap_check(!(sym->flags & KC_SYM_CHANGED));

		sym = kc_symtab_lookup(&tab, "HEX_OPT");
		tap_check(sym != NULL);
		tap_check(!(sym->flags & KC_SYM_CHANGED));

		sym = kc_symtab_lookup(&tab, "STR_OPT");
		tap_check(sym != NULL);
		tap_check(!(sym->flags & KC_SYM_CHANGED));

		remove(path);
		free(path);
		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("KC_SYM_CHANGED not set on rejected lines");
	}

	return tap_result();
}
