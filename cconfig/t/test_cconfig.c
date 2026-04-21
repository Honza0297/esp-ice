/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for the cconfig symbol table, expressions, and properties.
 */
#include "ice.h"
#include "cconfig/cconfig.h"
#include "tap.h"

int main(void)
{
	/* Symbol table init and release. */
	{
		struct kc_symtab tab;
		kc_symtab_init(&tab);
		tap_check(kc_sym_yes != NULL);
		tap_check(kc_sym_no != NULL);
		kc_symtab_release(&tab);
		tap_check(kc_sym_yes == NULL);
		tap_check(kc_sym_no == NULL);
		tap_done("symtab init and release");
	}

	/* Constant symbols are pre-interned with correct types and values. */
	{
		struct kc_symtab tab;
		kc_symtab_init(&tab);

		tap_check(strcmp(kc_sym_yes->name, "y") == 0);
		tap_check(kc_sym_yes->type == KC_TYPE_BOOL);
		tap_check(kc_sym_yes->flags & KC_SYM_CONST);
		tap_check(strcmp(kc_sym_yes->curr_value, "y") == 0);

		tap_check(strcmp(kc_sym_no->name, "n") == 0);
		tap_check(kc_sym_no->type == KC_TYPE_BOOL);
		tap_check(kc_sym_no->flags & KC_SYM_CONST);
		tap_check(strcmp(kc_sym_no->curr_value, "n") == 0);

		kc_symtab_release(&tab);
		tap_done("constant symbols y/n pre-interned");
	}

	/* Intern creates a new symbol; second intern returns the same pointer. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym_a, *sym_b;
		kc_symtab_init(&tab);

		sym_a = kc_symtab_intern(&tab, "FOO");
		tap_check(sym_a != NULL);
		tap_check(strcmp(sym_a->name, "FOO") == 0);
		tap_check(sym_a->type == KC_TYPE_UNKNOWN);

		sym_b = kc_symtab_intern(&tab, "FOO");
		tap_check(sym_b == sym_a);

		kc_symtab_release(&tab);
		tap_done("intern creates once, returns same pointer");
	}

	/* Lookup returns NULL for unknown, symbol for known. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym;
		kc_symtab_init(&tab);

		tap_check(kc_symtab_lookup(&tab, "UNKNOWN") == NULL);

		kc_symtab_intern(&tab, "BAR");
		sym = kc_symtab_lookup(&tab, "BAR");
		tap_check(sym != NULL);
		tap_check(strcmp(sym->name, "BAR") == 0);

		tap_check(kc_symtab_lookup(&tab, "BAZ") == NULL);

		kc_symtab_release(&tab);
		tap_done("lookup returns NULL for unknown, symbol for known");
	}

	/* Constant symbols are discoverable via lookup. */
	{
		struct kc_symtab tab;
		kc_symtab_init(&tab);

		tap_check(kc_symtab_lookup(&tab, "y") == kc_sym_yes);
		tap_check(kc_symtab_lookup(&tab, "n") == kc_sym_no);

		kc_symtab_release(&tab);
		tap_done("constant symbols found via lookup");
	}

	/* Add properties to a symbol and verify the list. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym;
		struct kc_property *prop;
		kc_symtab_init(&tab);

		sym = kc_symtab_intern(&tab, "PROP_TEST");
		tap_check(sym->props == NULL);

		kc_sym_add_prop(sym, KC_PROP_DEFAULT);
		kc_sym_add_prop(sym, KC_PROP_PROMPT);
		kc_sym_add_prop(sym, KC_PROP_SELECT);

		prop = sym->props;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_DEFAULT);

		prop = prop->next;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_PROMPT);

		prop = prop->next;
		tap_check(prop != NULL);
		tap_check(prop->kind == KC_PROP_SELECT);

		tap_check(prop->next == NULL);

		kc_symtab_release(&tab);
		tap_done("add_prop appends to property list in order");
	}

	/* sym_type_name returns correct strings. */
	{
		tap_check(strcmp(kc_sym_type_name(KC_TYPE_UNKNOWN), "unknown") == 0);
		tap_check(strcmp(kc_sym_type_name(KC_TYPE_BOOL), "bool") == 0);
		tap_check(strcmp(kc_sym_type_name(KC_TYPE_INT), "int") == 0);
		tap_check(strcmp(kc_sym_type_name(KC_TYPE_HEX), "hex") == 0);
		tap_check(strcmp(kc_sym_type_name(KC_TYPE_STRING), "string") == 0);
		tap_check(strcmp(kc_sym_type_name(KC_TYPE_FLOAT), "float") == 0);
		tap_done("sym_type_name returns correct strings");
	}

	/* Expression tree: symbol leaf node. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym;
		struct kc_expr *expr;
		struct sbuf sb = SBUF_INIT;
		kc_symtab_init(&tab);

		sym = kc_symtab_intern(&tab, "A");
		expr = kc_expr_alloc_sym(sym);
		tap_check(expr->type == KC_E_SYMBOL);
		tap_check(expr->data.sym == sym);

		kc_expr_print(expr, &sb);
		tap_check(strcmp(sb.buf, "A") == 0);

		kc_expr_free(expr);
		sbuf_release(&sb);
		kc_symtab_release(&tab);
		tap_done("expression: symbol leaf node");
	}

	/* Expression tree: NOT node. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym;
		struct kc_expr *expr;
		struct sbuf sb = SBUF_INIT;
		kc_symtab_init(&tab);

		sym = kc_symtab_intern(&tab, "B");
		expr = kc_expr_alloc(KC_E_NOT, kc_expr_alloc_sym(sym), NULL);

		kc_expr_print(expr, &sb);
		tap_check(strcmp(sb.buf, "!B") == 0);

		kc_expr_free(expr);
		sbuf_release(&sb);
		kc_symtab_release(&tab);
		tap_done("expression: NOT node");
	}

	/* Expression tree: AND/OR compound. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym_x, *sym_y, *sym_z;
		struct kc_expr *expr;
		struct sbuf sb = SBUF_INIT;
		kc_symtab_init(&tab);

		sym_x = kc_symtab_intern(&tab, "X");
		sym_y = kc_symtab_intern(&tab, "Y");
		sym_z = kc_symtab_intern(&tab, "Z");

		/* (X && Y) || Z */
		expr = kc_expr_alloc(
			KC_E_OR,
			kc_expr_alloc(KC_E_AND,
				      kc_expr_alloc_sym(sym_x),
				      kc_expr_alloc_sym(sym_y)),
			kc_expr_alloc_sym(sym_z));

		kc_expr_print(expr, &sb);
		tap_check(strcmp(sb.buf, "((X && Y) || Z)") == 0);

		kc_expr_free(expr);
		sbuf_release(&sb);
		kc_symtab_release(&tab);
		tap_done("expression: AND/OR compound tree");
	}

	/* Expression tree: comparison with alloc_comp. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym_a, *sym_b;
		struct kc_expr *expr;
		struct sbuf sb = SBUF_INIT;
		kc_symtab_init(&tab);

		sym_a = kc_symtab_intern(&tab, "A");
		sym_b = kc_symtab_intern(&tab, "B");
		expr = kc_expr_alloc_comp(KC_E_EQUAL, sym_a, sym_b);

		kc_expr_print(expr, &sb);
		tap_check(strcmp(sb.buf, "(A = B)") == 0);

		sbuf_reset(&sb);
		kc_expr_free(expr);

		expr = kc_expr_alloc_comp(KC_E_NOT_EQUAL, sym_a, sym_b);
		kc_expr_print(expr, &sb);
		tap_check(strcmp(sb.buf, "(A != B)") == 0);

		sbuf_reset(&sb);
		kc_expr_free(expr);

		expr = kc_expr_alloc_comp(KC_E_LTE, sym_a, sym_b);
		kc_expr_print(expr, &sb);
		tap_check(strcmp(sb.buf, "(A <= B)") == 0);

		kc_expr_free(expr);
		sbuf_release(&sb);
		kc_symtab_release(&tab);
		tap_done("expression: comparison nodes via alloc_comp");
	}

	/* Expression print round-trip: complex nested tree. */
	{
		struct kc_symtab tab;
		struct kc_symbol *sym_p, *sym_q, *sym_r, *sym_s;
		struct kc_expr *expr;
		struct sbuf sb = SBUF_INIT;
		kc_symtab_init(&tab);

		sym_p = kc_symtab_intern(&tab, "P");
		sym_q = kc_symtab_intern(&tab, "Q");
		sym_r = kc_symtab_intern(&tab, "R");
		sym_s = kc_symtab_intern(&tab, "S");

		/* !P && (Q = R) || S  →  ((!P && (Q = R)) || S) */
		expr = kc_expr_alloc(
			KC_E_OR,
			kc_expr_alloc(
				KC_E_AND,
				kc_expr_alloc(KC_E_NOT,
					      kc_expr_alloc_sym(sym_p), NULL),
				kc_expr_alloc_comp(KC_E_EQUAL, sym_q, sym_r)),
			kc_expr_alloc_sym(sym_s));

		kc_expr_print(expr, &sb);
		tap_check(strcmp(sb.buf, "((!P && (Q = R)) || S)") == 0);

		kc_expr_free(expr);
		sbuf_release(&sb);
		kc_symtab_release(&tab);
		tap_done("expression: complex nested print round-trip");
	}

	/* Hash collision handling: intern many symbols in same table. */
	{
		struct kc_symtab tab;
		char name[32];
		int idx;
		int all_found = 1;
		kc_symtab_init(&tab);

		for (idx = 0; idx < 1000; idx++) {
			sprintf(name, "SYM_%d", idx);
			kc_symtab_intern(&tab, name);
		}

		for (idx = 0; idx < 1000; idx++) {
			struct kc_symbol *sym;
			sprintf(name, "SYM_%d", idx);
			sym = kc_symtab_lookup(&tab, name);
			if (!sym || strcmp(sym->name, name) != 0) {
				all_found = 0;
				break;
			}
		}

		tap_check(all_found);
		tap_check(kc_symtab_lookup(&tab, "SYM_9999") == NULL);

		kc_symtab_release(&tab);
		tap_done("hash collision handling: 1000 symbols");
	}

	return tap_result();
}
