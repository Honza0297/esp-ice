/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_parser_ext.c
 * @brief Unit tests for parser extensions: choice, menuconfig, if/endif,
 *        and source/rsource/osource/orsource directives.
 */
#include "ice.h"
#include "cconfig/cconfig.h"
#include "tap.h"

static void write_file(const char *path, const char *content)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		die_errno("cannot write '%s'", path);
	fputs(content, fp);
	fclose(fp);
}

int main(void)
{
	/* 1. Choice block: two config members with KC_SYM_CHOICE/CHOICEVAL. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *choice_node, *child;
		struct kc_symbol *choice_sym, *sym_a, *sym_b;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"choice\n"
			"  bool\n"
			"config A\n"
			"  bool\n"
			"config B\n"
			"  bool\n"
			"endchoice\n",
			"test", &tab);

		tap_check(root->child != NULL);
		choice_node = root->child;
		choice_sym = choice_node->sym;

		tap_check(choice_sym != NULL);
		tap_check(choice_sym->flags & KC_SYM_CHOICE);
		tap_check(choice_sym->type == KC_TYPE_BOOL);

		child = choice_node->child;
		tap_check(child != NULL);
		sym_a = child->sym;
		tap_check(sym_a != NULL);
		tap_check(strcmp(sym_a->name, "A") == 0);
		tap_check(sym_a->flags & KC_SYM_CHOICEVAL);

		child = child->next;
		tap_check(child != NULL);
		sym_b = child->sym;
		tap_check(sym_b != NULL);
		tap_check(strcmp(sym_b->name, "B") == 0);
		tap_check(sym_b->flags & KC_SYM_CHOICEVAL);

		tap_check(child->next == NULL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("choice block with two config members");
	}

	/* 2. Choice with prompt and default. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *choice_node;
		struct kc_symbol *choice_sym;
		struct kc_property *prop;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"choice\n"
			"  prompt \"Pick\"\n"
			"  default B\n"
			"config A\n"
			"  bool\n"
			"config B\n"
			"  bool\n"
			"endchoice\n",
			"test", &tab);

		choice_node = root->child;
		tap_check(choice_node != NULL);
		tap_check(choice_node->prompt != NULL);
		tap_check(strcmp(choice_node->prompt, "Pick") == 0);

		choice_sym = choice_node->sym;
		tap_check(choice_sym != NULL);
		tap_check(choice_sym->flags & KC_SYM_CHOICE);

		prop = choice_sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_PROMPT);
		tap_check(prop->value != NULL);
		tap_check(strcmp(prop->value->data.sym->name, "Pick") == 0);

		prop = prop->next;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_DEFAULT);
		tap_check(prop->value != NULL);
		tap_check(prop->value->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->value->data.sym->name, "B") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("choice with prompt and default");
	}

	/* 3. menuconfig: is_menuconfig = 1, otherwise same as config. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *mc_node;
		struct kc_symbol *sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"menuconfig MC\n"
			"  bool \"Enable MC\"\n"
			"  default y\n",
			"test", &tab);

		mc_node = root->child;
		tap_check(mc_node != NULL);
		tap_check(mc_node->is_menuconfig == 1);

		sym = mc_node->sym;
		tap_check(sym != NULL);
		tap_check(strcmp(sym->name, "MC") == 0);
		tap_check(sym->type == KC_TYPE_BOOL);

		tap_check(mc_node->prompt != NULL);
		tap_check(strcmp(mc_node->prompt, "Enable MC") == 0);

		tap_check(sym->props != NULL);
		tap_check(sym->props->kind == KC_PROP_PROMPT);
		tap_check(sym->props->next != NULL);
		tap_check(sym->props->next->kind == KC_PROP_DEFAULT);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("menuconfig sets is_menuconfig = 1");
	}

	/* 4. if/endif: child under if-node with visibility. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *if_node, *child;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"if FOO\n"
			"config A\n"
			"  bool\n"
			"endif\n",
			"test", &tab);

		if_node = root->child;
		tap_check(if_node != NULL);
		tap_check(if_node->sym == NULL);
		tap_check(if_node->prompt == NULL);
		tap_check(if_node->is_menuconfig == 0);

		tap_check(if_node->visibility != NULL);
		tap_check(if_node->visibility->type == KC_E_SYMBOL);
		tap_check(strcmp(if_node->visibility->data.sym->name,
				"FOO") == 0);

		child = if_node->child;
		tap_check(child != NULL);
		tap_check(child->sym != NULL);
		tap_check(strcmp(child->sym->name, "A") == 0);
		tap_check(child->sym->type == KC_TYPE_BOOL);

		tap_check(child->next == NULL);
		tap_check(if_node->next == NULL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("if/endif: child under if-node with visibility");
	}

	/* 5. Nested if/endif. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *outer, *inner, *child;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"if A\n"
			"if B\n"
			"config C\n"
			"  bool\n"
			"endif\n"
			"endif\n",
			"test", &tab);

		outer = root->child;
		tap_check(outer != NULL);
		tap_check(outer->visibility != NULL);
		tap_check(outer->visibility->type == KC_E_SYMBOL);
		tap_check(strcmp(outer->visibility->data.sym->name,
				"A") == 0);

		inner = outer->child;
		tap_check(inner != NULL);
		tap_check(inner->visibility != NULL);
		tap_check(inner->visibility->type == KC_E_SYMBOL);
		tap_check(strcmp(inner->visibility->data.sym->name,
				"B") == 0);

		child = inner->child;
		tap_check(child != NULL);
		tap_check(child->sym != NULL);
		tap_check(strcmp(child->sym->name, "C") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("nested if/endif");
	}

	/* 6. source directive: include another Kconfig file. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;

		write_file("sub.kconfig",
			   "config FROM_SUB\n"
			   "  bool\n");

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config MAIN_SYM\n"
			"  bool\n"
			"source \"sub.kconfig\"\n",
			"test", &tab);

		tap_check(kc_symtab_lookup(&tab, "MAIN_SYM") != NULL);
		tap_check(kc_symtab_lookup(&tab, "FROM_SUB") != NULL);

		tap_check(root->child != NULL);
		tap_check(root->child->sym != NULL);
		tap_check(strcmp(root->child->sym->name, "MAIN_SYM") == 0);

		tap_check(root->child->next != NULL);
		tap_check(root->child->next->sym != NULL);
		tap_check(strcmp(root->child->next->sym->name,
				"FROM_SUB") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("source directive includes another file");
	}

	/* 7. osource non-existent: no error. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"osource \"nonexistent.kconfig\"\n"
			"config AFTER\n"
			"  bool\n",
			"test", &tab);

		tap_check(kc_symtab_lookup(&tab, "AFTER") != NULL);
		tap_check(root->child != NULL);
		tap_check(root->child->sym != NULL);
		tap_check(strcmp(root->child->sym->name, "AFTER") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("osource non-existent: no error");
	}

	/* 8. rsource: relative path inclusion. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;

		write_file("rel.kconfig",
			   "config REL_SYM\n"
			   "  int\n");

		write_file("main_rsource.kconfig",
			   "mainmenu \"T\"\n"
			   "rsource \"rel.kconfig\"\n");

		kc_symtab_init(&tab);
		root = kc_parse_file("main_rsource.kconfig", &tab);

		tap_check(kc_symtab_lookup(&tab, "REL_SYM") != NULL);
		tap_check(kc_symtab_lookup(&tab, "REL_SYM")->type ==
			  KC_TYPE_INT);

		tap_check(root->child != NULL);
		tap_check(root->child->sym != NULL);
		tap_check(strcmp(root->child->sym->name, "REL_SYM") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("rsource: relative path inclusion");
	}

	/* $(VAR) expansion in source path. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		FILE *fp;

		fp = fopen("envvar_included.kconfig", "w");
		fprintf(fp, "config ENV_SYM\n  bool\n");
		fclose(fp);

		setenv("KC_TEST_DIR", ".", 1);

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"source \"$(KC_TEST_DIR)/envvar_included.kconfig\"\n",
			"test", &tab);

		tap_check(kc_symtab_lookup(&tab, "ENV_SYM") != NULL);

		unsetenv("KC_TEST_DIR");
		kc_menu_free(root);
		kc_symtab_release(&tab);
		remove("envvar_included.kconfig");
		tap_done("$(VAR) expansion in source path");
	}

	/* if inside choice: CHOICEVAL propagates to nested configs. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym_a, *sym_b;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"choice\n"
			"  bool\n"
			"  if COND\n"
			"  config A\n"
			"    bool\n"
			"  endif\n"
			"  config B\n"
			"    bool\n"
			"endchoice\n",
			"test", &tab);

		sym_a = kc_symtab_lookup(&tab, "A");
		sym_b = kc_symtab_lookup(&tab, "B");
		tap_check(sym_a != NULL);
		tap_check(sym_b != NULL);
		tap_check(sym_a->flags & KC_SYM_CHOICEVAL);
		tap_check(sym_b->flags & KC_SYM_CHOICEVAL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("if inside choice: CHOICEVAL propagates");
	}

	return tap_result();
}
