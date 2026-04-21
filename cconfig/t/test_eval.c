/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_eval.c
 * @brief Unit tests for symbol evaluation and value computation.
 */
#include "ice.h"
#include "cconfig/cconfig.h"
#include "tap.h"

int main(void)
{
	/* 1. Bool symbol with default y → value is "y". */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  bool\n"
			"  default y\n",
			"test", &tab);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "FOO");
		tap_check(sym != NULL);
		tap_check(sym->curr_value != NULL);
		tap_check(strcmp(sym->curr_value, "y") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("bool with default y → value is y");
	}

	/* 2. Bool with default y if COND (COND is n) → value is "n". */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config COND\n"
			"  bool\n"
			"  default n\n"
			"\n"
			"config FOO\n"
			"  bool\n"
			"  default y if COND\n",
			"test", &tab);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "FOO");
		tap_check(sym != NULL);
		tap_check(sym->curr_value != NULL);
		tap_check(strcmp(sym->curr_value, "n") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("bool default y if COND (COND=n) → value is n");
	}

	/* 3. Int with range clamping: default 1000, range 0 100 → 100. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config VAL\n"
			"  int\n"
			"  default 1000\n"
			"  range 0 100\n",
			"test", &tab);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "VAL");
		tap_check(sym != NULL);
		tap_check(sym->curr_value != NULL);
		tap_check(strcmp(sym->curr_value, "100") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("int range clamping: default 1000, range 0 100 → 100");
	}

	/* 4. Select forces bool to y regardless of default. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *target;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config SELECTOR\n"
			"  bool\n"
			"  default y\n"
			"  select TARGET\n"
			"\n"
			"config TARGET\n"
			"  bool\n"
			"  default n\n",
			"test", &tab);

		kc_finalize(root, &tab);

		target = kc_symtab_lookup(&tab, "TARGET");
		tap_check(target != NULL);
		tap_check(target->curr_value != NULL);
		tap_check(strcmp(target->curr_value, "y") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("select forces bool to y");
	}

	/* 5. Expression evaluation: AND, OR, NOT with known values. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym_a, *sym_b;
		struct kc_expr *expr;

		kc_symtab_init(&tab);

		sym_a = kc_symtab_intern(&tab, "A");
		sym_a->type = KC_TYPE_BOOL;
		sym_a->curr_value = sbuf_strdup("y");
		sym_a->flags |= KC_SYM_VALID;

		sym_b = kc_symtab_intern(&tab, "B");
		sym_b->type = KC_TYPE_BOOL;
		sym_b->curr_value = sbuf_strdup("n");
		sym_b->flags |= KC_SYM_VALID;

		/* A && B → N (y && n) */
		expr = kc_expr_alloc(KC_E_AND,
				     kc_expr_alloc_sym(sym_a),
				     kc_expr_alloc_sym(sym_b));
		tap_check(kc_expr_eval(expr) == KC_VAL_N);
		kc_expr_free(expr);

		/* A || B → Y (y || n) */
		expr = kc_expr_alloc(KC_E_OR,
				     kc_expr_alloc_sym(sym_a),
				     kc_expr_alloc_sym(sym_b));
		tap_check(kc_expr_eval(expr) == KC_VAL_Y);
		kc_expr_free(expr);

		/* !B → Y */
		expr = kc_expr_alloc(KC_E_NOT,
				     kc_expr_alloc_sym(sym_b), NULL);
		tap_check(kc_expr_eval(expr) == KC_VAL_Y);
		kc_expr_free(expr);

		/* !A → N */
		expr = kc_expr_alloc(KC_E_NOT,
				     kc_expr_alloc_sym(sym_a), NULL);
		tap_check(kc_expr_eval(expr) == KC_VAL_N);
		kc_expr_free(expr);

		kc_symtab_release(&tab);
		tap_done("expression eval: AND, OR, NOT");
	}

	/* 6. String comparison: FOO = "bar" with FOO set to "bar" → Y. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym_foo, *sym_bar, *sym_baz;
		struct kc_expr *expr;

		kc_symtab_init(&tab);

		sym_foo = kc_symtab_intern(&tab, "FOO");
		sym_foo->type = KC_TYPE_STRING;
		sym_foo->curr_value = sbuf_strdup("bar");
		sym_foo->flags |= KC_SYM_VALID;

		/* "bar" has no menu_node → string constant with value "bar" */
		sym_bar = kc_symtab_intern(&tab, "bar");

		expr = kc_expr_alloc_comp(KC_E_EQUAL, sym_foo, sym_bar);
		tap_check(kc_expr_eval(expr) == KC_VAL_Y);
		kc_expr_free(expr);

		/* Different value: FOO != "baz" */
		sym_baz = kc_symtab_intern(&tab, "baz");
		expr = kc_expr_alloc_comp(KC_E_EQUAL, sym_foo, sym_baz);
		tap_check(kc_expr_eval(expr) == KC_VAL_N);
		kc_expr_free(expr);

		kc_symtab_release(&tab);
		tap_done("string comparison FOO = bar → Y");
	}

	/* 7. Visibility: menu node with depends on FALSE → not visible. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *child;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config GATE\n"
			"  bool\n"
			"  default n\n"
			"\n"
			"config FOO\n"
			"  bool\n"
			"  depends on GATE\n"
			"  default y\n",
			"test", &tab);

		kc_finalize(root, &tab);

		/* First child is GATE, second is FOO */
		child = root->child;
		tap_check(child != NULL);
		child = child->next;
		tap_check(child != NULL);
		tap_check(child->sym != NULL);
		tap_check(strcmp(child->sym->name, "FOO") == 0);
		tap_check(!kc_menu_visible(child));

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("visibility: depends on FALSE → not visible");
	}

	/* 8. Default priority: first matching default wins. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config FOO\n"
			"  bool\n"
			"  default n\n"
			"  default y\n",
			"test", &tab);

		kc_finalize(root, &tab);

		sym = kc_symtab_lookup(&tab, "FOO");
		tap_check(sym != NULL);
		tap_check(sym->curr_value != NULL);
		tap_check(strcmp(sym->curr_value, "n") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("default priority: first matching wins");
	}

	/* 9. Imply: suggests y but doesn't force. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root;
		struct kc_symbol *target;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config IMPLIER\n"
			"  bool\n"
			"  default y\n"
			"  imply TARGET\n"
			"\n"
			"config TARGET\n"
			"  bool\n"
			"  default n\n",
			"test", &tab);

		kc_finalize(root, &tab);

		target = kc_symtab_lookup(&tab, "TARGET");
		tap_check(target != NULL);
		tap_check(target->curr_value != NULL);
		tap_check(strcmp(target->curr_value, "y") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("imply suggests y (default n → becomes y)");
	}

	/* 10. Finalize: parent menu depends on propagates to children. */
	{
		struct kc_symtab tab;
		struct kc_menu_node *root, *menu_node, *child_node;
		struct kc_symbol *gate, *child_sym;

		kc_symtab_init(&tab);
		root = kc_parse_buffer(
			"mainmenu \"T\"\n"
			"config GATE\n"
			"  bool\n"
			"  default n\n"
			"\n"
			"menu \"Sub\"\n"
			"  depends on GATE\n"
			"\n"
			"config CHILD\n"
			"  bool\n"
			"  default y\n"
			"endmenu\n",
			"test", &tab);

		kc_finalize(root, &tab);

		gate = kc_symtab_lookup(&tab, "GATE");
		tap_check(gate != NULL);
		tap_check(strcmp(gate->curr_value, "n") == 0);

		/* Menu node is second child of root (after GATE config) */
		menu_node = root->child->next;
		tap_check(menu_node != NULL);
		tap_check(!kc_menu_visible(menu_node));

		/* CHILD inherited GATE dependency from parent menu */
		child_node = menu_node->child;
		tap_check(child_node != NULL);
		child_sym = child_node->sym;
		tap_check(child_sym != NULL);
		tap_check(strcmp(child_sym->name, "CHILD") == 0);
		tap_check(!kc_menu_visible(child_node));

		/*
		 * CHILD's default y has inherited cond GATE; since
		 * GATE=n the default does not apply → type default "n".
		 */
		tap_check(strcmp(child_sym->curr_value, "n") == 0);

		kc_menu_free(root);
		kc_symtab_release(&tab);
		tap_done("finalize: parent depends on propagates to children");
	}

	return tap_result();
}
