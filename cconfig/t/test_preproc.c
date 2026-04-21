/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_preproc.c
 * @brief Unit tests for the Kconfig macro/variable preprocessor.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include "ice.h"
#include "cconfig/cconfig.h"
#include "tap.h"

#include <stdlib.h>

int main(void)
{
	/* 1. Simple variable expansion: FOO = bar, $(FOO) -> bar */
	{
		struct kc_symtab tab;
		char *result;

		kc_symtab_init(&tab);
		kc_preproc_set(&tab, "FOO", "bar", 0);
		result = kc_preproc_expand(&tab, "$(FOO)");

		tap_check(strcmp(result, "bar") == 0);

		free(result);

		result = kc_preproc_expand(&tab, "prefix_$(FOO)_suffix");
		tap_check(strcmp(result, "prefix_bar_suffix") == 0);
		free(result);

		kc_symtab_release(&tab);
		tap_done("simple variable expansion");
	}

	/* 2. Environment variable fallback */
	{
		struct kc_symtab tab;
		char *result;

		kc_symtab_init(&tab);

		setenv("KC_TEST_ENVVAR", "from_env", 1);
		result = kc_preproc_expand(&tab, "$(KC_TEST_ENVVAR)");
		tap_check(strcmp(result, "from_env") == 0);
		free(result);

		unsetenv("KC_TEST_ENVVAR");
		kc_symtab_release(&tab);
		tap_done("environment variable fallback");
	}

	/* 3. Kconfig variable overrides environment variable */
	{
		struct kc_symtab tab;
		char *result;

		kc_symtab_init(&tab);

		setenv("FOO", "global", 1);
		kc_preproc_set(&tab, "FOO", "local", 0);
		result = kc_preproc_expand(&tab, "$(FOO)");
		tap_check(strcmp(result, "local") == 0);
		free(result);

		unsetenv("FOO");
		kc_symtab_release(&tab);
		tap_done("variable overrides environment");
	}

	/* 4. Undefined variable expands to empty string */
	{
		struct kc_symtab tab;
		char *result;

		kc_symtab_init(&tab);

		unsetenv("TOTALLY_UNDEFINED_VAR_XYZ");
		result = kc_preproc_expand(&tab, "a$(TOTALLY_UNDEFINED_VAR_XYZ)b");
		tap_check(strcmp(result, "ab") == 0);
		free(result);

		kc_symtab_release(&tab);
		tap_done("undefined variable expands to empty");
	}

	/* 5. Nested $(...) is not recursively parsed inside the name */
	{
		struct kc_symtab tab;
		char *result;

		kc_symtab_init(&tab);
		kc_preproc_set(&tab, "INNER", "X", 0);
		kc_preproc_set(&tab, "X", "should_not_appear", 0);

		/*
		 * $(A$(INNER)) — the first ')' closes at INNER, so the
		 * outer name becomes "A$(INNER" which won't match anything.
		 * The ')' after INNER closes the inner reference, leaving
		 * a trailing ')' as literal text.
		 *
		 * Actually, the parser finds the first ')' and treats
		 * "A$(INNER" as the name.  That won't match, so it expands
		 * to empty, then the trailing ')' is literal.
		 */
		result = kc_preproc_expand(&tab, "$(A$(INNER))");
		tap_check(strcmp(result, ")") == 0);
		free(result);

		kc_symtab_release(&tab);
		tap_done("nested $(...) not supported — documented behavior");
	}

	/* 6. := immediate expansion */
	{
		struct kc_symtab tab;
		char *result;

		kc_symtab_init(&tab);

		kc_preproc_set(&tab, "BASE", "hello", 0);
		kc_preproc_set(&tab, "DERIVED", "$(BASE)_world", 1);

		/* DERIVED was expanded at set-time, so changing BASE later
		 * does not affect the already-expanded value. */
		kc_preproc_set(&tab, "BASE", "changed", 0);

		result = kc_preproc_expand(&tab, "$(DERIVED)");
		tap_check(strcmp(result, "hello_world") == 0);
		free(result);

		kc_symtab_release(&tab);
		tap_done(":= immediate expansion");
	}

	/* 7. Variable assignment via parser (integration) */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"FOO = bar\n"
			"mainmenu \"$(FOO)\"\n",
			"test", &tab);

		tap_check(root->prompt != NULL);
		tap_check(strcmp(root->prompt, "bar") == 0);

		/* Verify the variable is in the table */
		sym = kc_symtab_lookup(&tab, "bar");
		tap_check(sym != NULL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("variable assignment via parser");
	}

	/* 8. := assignment via parser */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"BASE = hello\n"
			"FULL := $(BASE)_world\n"
			"BASE = changed\n"
			"mainmenu \"$(FULL)\"\n",
			"test", &tab);

		tap_check(root->prompt != NULL);
		tap_check(strcmp(root->prompt, "hello_world") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done(":= assignment via parser");
	}

	/* 9. Variable expansion in source path */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		FILE *fp;

		fp = fopen("preproc_sub.kconfig", "w");
		fprintf(fp, "config FROM_PREPROC\n  bool\n");
		fclose(fp);

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"KDIR = .\n"
			"mainmenu \"T\"\n"
			"source \"$(KDIR)/preproc_sub.kconfig\"\n",
			"test", &tab);

		tap_check(kc_symtab_lookup(&tab, "FROM_PREPROC") != NULL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		remove("preproc_sub.kconfig");
		tap_done("variable expansion in source path");
	}

	/* Immediate self-reference: FOO := $(FOO)_suffix after FOO exists. */
	{
		struct kc_symtab tab;
		char *result;

		kc_symtab_init(&tab);
		kc_preproc_set(&tab, "SELF", "hello", 0);
		kc_preproc_set(&tab, "SELF", "$(SELF)_world", 1);

		result = kc_preproc_expand(&tab, "$(SELF)");
		tap_check(strcmp(result, "hello_world") == 0);
		free(result);

		kc_symtab_release(&tab);
		tap_done("immediate self-reference expands old value");
	}

	return tap_result();
}
