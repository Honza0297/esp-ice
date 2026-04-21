/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file expr.c
 * @brief Expression tree construction, destruction, and printing.
 */
#include "ice.h"
#include "cconfig/cconfig.h"

struct kc_expr *kc_expr_alloc(enum kc_expr_type type,
			      struct kc_expr *left,
			      struct kc_expr *right)
{
	struct kc_expr *expr = xcalloc(1, sizeof(*expr));
	expr->type = type;
	expr->data.children.left = left;
	expr->data.children.right = right;
	return expr;
}

struct kc_expr *kc_expr_alloc_sym(struct kc_symbol *sym)
{
	struct kc_expr *expr = xcalloc(1, sizeof(*expr));
	expr->type = KC_E_SYMBOL;
	expr->data.sym = sym;
	return expr;
}

static int is_comparison_type(enum kc_expr_type type)
{
	return type == KC_E_EQUAL || type == KC_E_NOT_EQUAL ||
	       type == KC_E_LT || type == KC_E_GT ||
	       type == KC_E_LTE || type == KC_E_GTE;
}

struct kc_expr *kc_expr_alloc_comp(enum kc_expr_type type,
				   struct kc_symbol *sym_left,
				   struct kc_symbol *sym_right)
{
	struct kc_expr *left, *right;

	if (!is_comparison_type(type))
		die("BUG: kc_expr_alloc_comp called with non-comparison type %d", type);

	left = kc_expr_alloc_sym(sym_left);
	right = kc_expr_alloc_sym(sym_right);
	return kc_expr_alloc(type, left, right);
}

void kc_expr_free(struct kc_expr *expr)
{
	if (!expr)
		return;

	switch (expr->type) {
	case KC_E_AND:
	case KC_E_OR:
	case KC_E_NOT:
	case KC_E_EQUAL:
	case KC_E_NOT_EQUAL:
	case KC_E_LT:
	case KC_E_GT:
	case KC_E_LTE:
	case KC_E_GTE:
		kc_expr_free(expr->data.children.left);
		kc_expr_free(expr->data.children.right);
		break;
	case KC_E_SYMBOL:
	case KC_E_NONE:
		break;
	}

	free(expr);
}

static const char *expr_op_str(enum kc_expr_type type)
{
	switch (type) {
	case KC_E_AND:       return "&&";
	case KC_E_OR:        return "||";
	case KC_E_NOT:       return "!";
	case KC_E_EQUAL:     return "=";
	case KC_E_NOT_EQUAL: return "!=";
	case KC_E_LT:        return "<";
	case KC_E_GT:        return ">";
	case KC_E_LTE:       return "<=";
	case KC_E_GTE:       return ">=";
	default:             return "?";
	}
}

void kc_expr_print(const struct kc_expr *expr, struct sbuf *sb)
{
	if (!expr) {
		sbuf_addstr(sb, "<null>");
		return;
	}

	switch (expr->type) {
	case KC_E_SYMBOL:
		sbuf_addstr(sb, expr->data.sym ? expr->data.sym->name : "<?>");
		break;
	case KC_E_NOT:
		sbuf_addch(sb, '!');
		kc_expr_print(expr->data.children.left, sb);
		break;
	case KC_E_AND:
	case KC_E_OR:
	case KC_E_EQUAL:
	case KC_E_NOT_EQUAL:
	case KC_E_LT:
	case KC_E_GT:
	case KC_E_LTE:
	case KC_E_GTE:
		sbuf_addch(sb, '(');
		kc_expr_print(expr->data.children.left, sb);
		sbuf_addch(sb, ' ');
		sbuf_addstr(sb, expr_op_str(expr->type));
		sbuf_addch(sb, ' ');
		kc_expr_print(expr->data.children.right, sb);
		sbuf_addch(sb, ')');
		break;
	case KC_E_NONE:
		sbuf_addstr(sb, "<none>");
		break;
	}
}
