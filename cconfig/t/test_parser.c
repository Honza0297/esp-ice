/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for the cconfig Kconfig parser.
 */
#include "ice.h"
#include "cconfig/cconfig.h"
#include "tap.h"

int main(void)
{
	/* 1. Minimal Kconfig: mainmenu + one config with bool and default. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		struct kc_property *prop;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"Test\"\n"
			"config FOO\n"
			"  bool\n"
			"  default y\n",
			"test", &tab);

		tap_check(root != NULL);
		tap_check(root->prompt != NULL);
		tap_check(strcmp(root->prompt, "Test") == 0);

		tap_check(root->child != NULL);
		tap_check(root->child->next == NULL);

		sym = root->child->sym;
		tap_check(sym != NULL);
		tap_check(strcmp(sym->name, "FOO") == 0);
		tap_check(sym->type == KC_TYPE_BOOL);

		prop = sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_DEFAULT);
		tap_check(prop->value != NULL);
		tap_check(prop->value->type == KC_E_SYMBOL);
		tap_check(prop->value->data.sym == kc_sym_yes);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("minimal Kconfig with mainmenu, config, bool, default");
	}

	/* 2. Multiple configs with different types. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *child;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"Multi\"\n"
			"config A\n"
			"  bool\n"
			"config B\n"
			"  int\n"
			"config C\n"
			"  string\n"
			"config D\n"
			"  hex\n"
			"config E\n"
			"  float\n",
			"test", &tab);

		tap_check(kc_symtab_lookup(&tab, "A") != NULL);
		tap_check(kc_symtab_lookup(&tab, "A")->type == KC_TYPE_BOOL);
		tap_check(kc_symtab_lookup(&tab, "B") != NULL);
		tap_check(kc_symtab_lookup(&tab, "B")->type == KC_TYPE_INT);
		tap_check(kc_symtab_lookup(&tab, "C") != NULL);
		tap_check(kc_symtab_lookup(&tab, "C")->type == KC_TYPE_STRING);
		tap_check(kc_symtab_lookup(&tab, "D") != NULL);
		tap_check(kc_symtab_lookup(&tab, "D")->type == KC_TYPE_HEX);
		tap_check(kc_symtab_lookup(&tab, "E") != NULL);
		tap_check(kc_symtab_lookup(&tab, "E")->type == KC_TYPE_FLOAT);

		child = root->child;
		tap_check(child != NULL);
		child = child->next;
		tap_check(child != NULL);
		child = child->next;
		tap_check(child != NULL);
		child = child->next;
		tap_check(child != NULL);
		child = child->next;
		tap_check(child != NULL);
		tap_check(child->next == NULL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("multiple configs with different types");
	}

	/* 3. Menu block: root -> menu -> config. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *menu_node;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"Root\"\n"
			"menu \"Sub\"\n"
			"config A\n"
			"  int\n"
			"endmenu\n",
			"test", &tab);

		tap_check(root->child != NULL);
		menu_node = root->child;
		tap_check(menu_node->sym == NULL);
		tap_check(menu_node->prompt != NULL);
		tap_check(strcmp(menu_node->prompt, "Sub") == 0);

		tap_check(menu_node->child != NULL);
		tap_check(menu_node->child->sym != NULL);
		tap_check(strcmp(menu_node->child->sym->name, "A") == 0);
		tap_check(menu_node->child->sym->type == KC_TYPE_INT);

		tap_check(menu_node->next == NULL);
		tap_check(menu_node->child->parent == menu_node);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("menu block nesting: root -> menu -> config");
	}

	/* 4. Comment entry with depends on. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *comment_node;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"comment \"A comment\"\n"
			"  depends on X\n",
			"test", &tab);

		tap_check(root->child != NULL);
		comment_node = root->child;
		tap_check(comment_node->sym == NULL);
		tap_check(comment_node->prompt != NULL);
		tap_check(strcmp(comment_node->prompt, "A comment") == 0);

		tap_check(comment_node->visibility != NULL);
		tap_check(comment_node->visibility->type == KC_E_SYMBOL);
		tap_check(strcmp(comment_node->visibility->data.sym->name,
				"X") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("comment entry with depends on");
	}

	/* 5. Expression parsing: depends on A && (B || !C). */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_expr *vis, *or_node, *not_node;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  bool\n"
			"  depends on A && (B || !C)\n",
			"test", &tab);

		tap_check(root->child != NULL);
		vis = root->child->visibility;
		tap_check(vis != NULL);
		tap_check(vis->type == KC_E_AND);

		tap_check(vis->data.children.left->type == KC_E_SYMBOL);
		tap_check(strcmp(vis->data.children.left->data.sym->name,
				"A") == 0);

		or_node = vis->data.children.right;
		tap_check(or_node->type == KC_E_OR);

		tap_check(or_node->data.children.left->type == KC_E_SYMBOL);
		tap_check(strcmp(or_node->data.children.left->data.sym->name,
				"B") == 0);

		not_node = or_node->data.children.right;
		tap_check(not_node->type == KC_E_NOT);
		tap_check(not_node->data.children.left->type == KC_E_SYMBOL);
		tap_check(strcmp(
			not_node->data.children.left->data.sym->name,
			"C") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("expression: depends on A && (B || !C)");
	}

	/* 6. Prompt with if condition. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		struct kc_property *prop;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  bool\n"
			"  prompt \"Foo\" if BAR\n",
			"test", &tab);

		sym = kc_symtab_lookup(&tab, "FOO");
		tap_check(sym != NULL);

		prop = sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_PROMPT);

		tap_check(prop->value != NULL);
		tap_check(prop->value->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->value->data.sym->name, "Foo") == 0);

		tap_check(prop->cond != NULL);
		tap_check(prop->cond->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->cond->data.sym->name, "BAR") == 0);

		tap_check(root->child->prompt != NULL);
		tap_check(strcmp(root->child->prompt, "Foo") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("prompt with if condition");
	}

	/* 7. Select and imply properties. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		struct kc_property *prop;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  bool\n"
			"  select BAZ if COND\n"
			"  imply QUX\n",
			"test", &tab);

		sym = kc_symtab_lookup(&tab, "FOO");
		tap_check(sym != NULL);

		prop = sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_SELECT);
		tap_check(prop->value != NULL);
		tap_check(prop->value->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->value->data.sym->name, "BAZ") == 0);
		tap_check(prop->cond != NULL);
		tap_check(prop->cond->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->cond->data.sym->name, "COND") == 0);

		prop = prop->next;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_IMPLY);
		tap_check(prop->value != NULL);
		tap_check(prop->value->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->value->data.sym->name, "QUX") == 0);
		tap_check(prop->cond == NULL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("select and imply properties");
	}

	/* 8. def_bool shorthand. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		struct kc_property *prop;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  def_bool y\n",
			"test", &tab);

		sym = kc_symtab_lookup(&tab, "FOO");
		tap_check(sym != NULL);
		tap_check(sym->type == KC_TYPE_BOOL);

		prop = sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_DEFAULT);
		tap_check(prop->value != NULL);
		tap_check(prop->value->type == KC_E_SYMBOL);
		tap_check(prop->value->data.sym == kc_sym_yes);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("def_bool shorthand sets type and default");
	}

	/* 9. Help text stored on menu node. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *config_node;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  bool\n"
			"  help\n"
			"    This is help.\n"
			"    Second line.\n",
			"test", &tab);

		config_node = root->child;
		tap_check(config_node != NULL);
		tap_check(config_node->help != NULL);
		tap_check(strcmp(config_node->help,
				"This is help.\nSecond line.") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("help text stored on menu node");
	}

	/* 10. depends on inheritance into prompt and default conditions. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *config_node;
		struct kc_symbol *sym;
		struct kc_property *prop;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  bool\n"
			"  prompt \"Enable\" if GUARD\n"
			"  default y\n"
			"  depends on DEP\n",
			"test", &tab);

		config_node = root->child;
		tap_check(config_node != NULL);

		/* Visibility should reference DEP. */
		tap_check(config_node->visibility != NULL);
		tap_check(config_node->visibility->type == KC_E_SYMBOL);
		tap_check(strcmp(config_node->visibility->data.sym->name,
				"DEP") == 0);

		sym = config_node->sym;
		tap_check(sym != NULL);

		/* Prompt condition: original GUARD && inherited DEP. */
		prop = sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_PROMPT);
		tap_check(prop->cond != NULL);
		tap_check(prop->cond->type == KC_E_AND);
		tap_check(prop->cond->data.children.left->type ==
			  KC_E_SYMBOL);
		tap_check(strcmp(
			prop->cond->data.children.left->data.sym->name,
			"GUARD") == 0);
		tap_check(prop->cond->data.children.right->type ==
			  KC_E_SYMBOL);
		tap_check(strcmp(
			prop->cond->data.children.right->data.sym->name,
			"DEP") == 0);

		/* Default condition: was NULL, now inherited DEP. */
		prop = prop->next;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_DEFAULT);
		tap_check(prop->cond != NULL);
		tap_check(prop->cond->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->cond->data.sym->name, "DEP") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("depends on inheritance into prompt and default");
	}

	/* range property uses KC_E_RANGE. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;
		struct kc_property *prop;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config VAL\n"
			"  int\n"
			"  range 0 100\n",
			"test", &tab);

		sym = kc_symtab_lookup(&tab, "VAL");
		tap_check(sym != NULL);
		tap_check(sym->type == KC_TYPE_INT);

		prop = sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_RANGE);
		tap_check(prop->value != NULL);
		tap_check(prop->value->type == KC_E_RANGE);
		tap_check(prop->value->data.children.left->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->value->data.children.left->data.sym->name, "0") == 0);
		tap_check(prop->value->data.children.right->type == KC_E_SYMBOL);
		tap_check(strcmp(prop->value->data.children.right->data.sym->name, "100") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("range property uses KC_E_RANGE");
	}

	/* Menu with depends on + visible if. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *menu_node;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"menu \"Sub\"\n"
			"  depends on A\n"
			"  visible if B\n"
			"config X\n"
			"  bool\n"
			"endmenu\n",
			"test", &tab);

		menu_node = root->child;
		tap_check(menu_node != NULL);
		tap_check(strcmp(menu_node->prompt, "Sub") == 0);
		tap_check(menu_node->visibility != NULL);
		tap_check(menu_node->visibility->type == KC_E_AND);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("menu with depends on + visible if");
	}

	/* Multiple depends on lines AND together. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *cfg_node;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config MULTI\n"
			"  bool\n"
			"  depends on A\n"
			"  depends on B\n",
			"test", &tab);

		cfg_node = root->child;
		tap_check(cfg_node != NULL);
		tap_check(cfg_node->visibility != NULL);
		tap_check(cfg_node->visibility->type == KC_E_AND);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("multiple depends on lines AND together");
	}

	/* warning property is silently ignored. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config W\n"
			"  bool\n"
			"  warning \"some text\"\n"
			"  default y\n",
			"test", &tab);

		sym = kc_symtab_lookup(&tab, "W");
		tap_check(sym != NULL);
		tap_check(sym->type == KC_TYPE_BOOL);
		tap_check(sym->props != NULL);
		tap_check(sym->props->kind == KC_PROP_DEFAULT);
		tap_check(sym->props->next == NULL);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("warning property is silently ignored");
	}

	return tap_result();
}
