/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file eval.c
 * @brief Symbol value computation and expression evaluation.
 *
 * Evaluates Kconfig expressions, computes symbol values from defaults,
 * select/imply reverse dependencies, and range constraints.  The
 * finalization pass propagates visibility and dependencies from
 * parent menus and if-blocks down to children.
 */
#include "cconfig/cconfig.h"
#include "ice.h"

/* ------------------------------------------------------------------ */
/*  Symbol string value                                                */
/* ------------------------------------------------------------------ */

static const char *sym_type_default(enum kc_sym_type type)
{
	switch (type) {
	case KC_TYPE_BOOL:
		return "n";
	case KC_TYPE_INT:
		return "0";
	case KC_TYPE_HEX:
		return "0x0";
	case KC_TYPE_STRING:
		return "";
	case KC_TYPE_FLOAT:
		return "0.0";
	default:
		return "";
	}
}

/**
 * Return the string representation of a symbol's value.
 * For symbols without a config entry (not in the Kconfig tree),
 * the symbol's name is used as its string value — this handles
 * quoted-string constants like `default "hello"` where "hello"
 * is interned as a nameless symbol.
 */
const char *kc_sym_get_string(const struct kc_symbol *sym)
{
	if (sym->curr_value)
		return sym->curr_value;
	if (!sym->menu_node)
		return sym->name;
	return sym_type_default(sym->type);
}

/* ------------------------------------------------------------------ */
/*  Expression evaluation                                              */
/* ------------------------------------------------------------------ */

enum kc_val kc_expr_eval(const struct kc_expr *expr)
{
	if (!expr)
		return KC_VAL_N;

	switch (expr->type) {
	case KC_E_SYMBOL: {
		const char *val;

		kc_sym_calc_value(expr->data.sym);
		val = kc_sym_get_string(expr->data.sym);
		return (strcmp(val, "y") == 0) ? KC_VAL_Y : KC_VAL_N;
	}
	case KC_E_AND:
		return (kc_expr_eval(expr->data.children.left) == KC_VAL_Y &&
			kc_expr_eval(expr->data.children.right) == KC_VAL_Y)
			   ? KC_VAL_Y
			   : KC_VAL_N;
	case KC_E_OR:
		return (kc_expr_eval(expr->data.children.left) == KC_VAL_Y ||
			kc_expr_eval(expr->data.children.right) == KC_VAL_Y)
			   ? KC_VAL_Y
			   : KC_VAL_N;
	case KC_E_NOT:
		return (kc_expr_eval(expr->data.children.left) == KC_VAL_Y)
			   ? KC_VAL_N
			   : KC_VAL_Y;
	case KC_E_EQUAL:
	case KC_E_NOT_EQUAL: {
		struct kc_symbol *lsym, *rsym;

		if (expr->data.children.left->type != KC_E_SYMBOL ||
		    expr->data.children.right->type != KC_E_SYMBOL)
			die("BUG: comparison operands must be KC_E_SYMBOL");
		lsym = expr->data.children.left->data.sym;
		rsym = expr->data.children.right->data.sym;
		const char *lval, *rval;
		int eq;

		kc_sym_calc_value(lsym);
		kc_sym_calc_value(rsym);
		lval = kc_sym_get_string(lsym);
		rval = kc_sym_get_string(rsym);
		eq = (strcmp(lval, rval) == 0);
		if (expr->type == KC_E_NOT_EQUAL)
			eq = !eq;
		return eq ? KC_VAL_Y : KC_VAL_N;
	}
	case KC_E_LT:
	case KC_E_GT:
	case KC_E_LTE:
	case KC_E_GTE: {
		struct kc_symbol *lsym, *rsym;

		if (expr->data.children.left->type != KC_E_SYMBOL ||
		    expr->data.children.right->type != KC_E_SYMBOL)
			die("BUG: relational operands must be KC_E_SYMBOL");
		lsym = expr->data.children.left->data.sym;
		rsym = expr->data.children.right->data.sym;
		long lnum, rnum;
		int result;

		kc_sym_calc_value(lsym);
		kc_sym_calc_value(rsym);
		lnum = strtol(kc_sym_get_string(lsym), NULL, 0);
		rnum = strtol(kc_sym_get_string(rsym), NULL, 0);

		switch (expr->type) {
		case KC_E_LT:
			result = (lnum < rnum);
			break;
		case KC_E_GT:
			result = (lnum > rnum);
			break;
		case KC_E_LTE:
			result = (lnum <= rnum);
			break;
		case KC_E_GTE:
			result = (lnum >= rnum);
			break;
		default:
			result = 0;
			break;
		}
		return result ? KC_VAL_Y : KC_VAL_N;
	}
	case KC_E_RANGE:
		die("BUG: KC_E_RANGE evaluated in boolean context");
	case KC_E_NONE:
		return KC_VAL_N;
	}

	return KC_VAL_N;
}

/* ------------------------------------------------------------------ */
/*  Range clamping                                                     */
/* ------------------------------------------------------------------ */

static void apply_range(struct kc_symbol *sym, const struct kc_property *prop)
{
	struct kc_symbol *lo_sym = prop->value->data.children.left->data.sym;
	struct kc_symbol *hi_sym = prop->value->data.children.right->data.sym;
	long val, lo_val, hi_val;
	char buf[64];

	kc_sym_calc_value(lo_sym);
	kc_sym_calc_value(hi_sym);

	val = strtol(sym->curr_value, NULL, 0);
	lo_val = strtol(kc_sym_get_string(lo_sym), NULL, 0);
	hi_val = strtol(kc_sym_get_string(hi_sym), NULL, 0);

	if (val < lo_val)
		val = lo_val;
	if (val > hi_val)
		val = hi_val;

	if (sym->type == KC_TYPE_HEX)
		snprintf(buf, sizeof(buf), "0x%lx", val);
	else
		snprintf(buf, sizeof(buf), "%ld", val);

	free(sym->curr_value);
	sym->curr_value = sbuf_strdup(buf);
}

/* ------------------------------------------------------------------ */
/*  Symbol value computation                                           */
/* ------------------------------------------------------------------ */

void kc_sym_calc_value(struct kc_symbol *sym)
{
	struct kc_property *prop;

	if (!sym || (sym->flags & KC_SYM_VALID))
		return;

	sym->flags |= KC_SYM_VALID;

	if (sym->flags & KC_SYM_CONST)
		return;

	/* Symbols with no config entry are string constants (value = name) */
	if (!sym->menu_node)
		return;

	if (!sym->curr_value) {
		const char *def_val = NULL;

		for (prop = sym->props; prop; prop = prop->next) {
			if (prop->kind != KC_PROP_DEFAULT)
				continue;
			if (prop->cond && kc_expr_eval(prop->cond) != KC_VAL_Y)
				continue;
			if (prop->value) {
				if (prop->value->type == KC_E_SYMBOL) {
					kc_sym_calc_value(
					    prop->value->data.sym);
					def_val = kc_sym_get_string(
					    prop->value->data.sym);
				} else {
					def_val = (kc_expr_eval(prop->value) ==
						   KC_VAL_Y)
						      ? "y"
						      : "n";
				}
			}
			break;
		}

		sym->curr_value = sbuf_strdup(
		    def_val ? def_val : sym_type_default(sym->type));
	}

	/* Bool: reverse dependencies override the computed value */
	if (sym->type == KC_TYPE_BOOL) {
		if (sym->rev_deps && kc_expr_eval(sym->rev_deps) == KC_VAL_Y) {
			free(sym->curr_value);
			sym->curr_value = sbuf_strdup("y");
		}
		if (sym->weak_rev_deps &&
		    kc_expr_eval(sym->weak_rev_deps) == KC_VAL_Y &&
		    strcmp(sym->curr_value, "n") == 0) {
			free(sym->curr_value);
			sym->curr_value = sbuf_strdup("y");
		}
	}

	/* Int/hex: clamp to first matching range */
	if (sym->type == KC_TYPE_INT || sym->type == KC_TYPE_HEX) {
		for (prop = sym->props; prop; prop = prop->next) {
			if (prop->kind != KC_PROP_RANGE)
				continue;
			if (prop->cond && kc_expr_eval(prop->cond) != KC_VAL_Y)
				continue;
			apply_range(sym, prop);
			break;
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Finalization — visibility propagation                              */
/* ------------------------------------------------------------------ */

/**
 * Propagate visibility down the menu tree.
 *
 * ANDs @p parent_vis into @p node's visibility and into any
 * PROMPT / DEFAULT conditions on the node's symbol. Then recurses
 * into children, passing the now-updated node->visibility as the
 * parent context for the next level.
 */
static void propagate_visibility(struct kc_menu_node *node,
				 const struct kc_expr *parent_vis)
{
	struct kc_menu_node *child;

	if (parent_vis) {
		if (node->visibility)
			node->visibility =
			    kc_expr_alloc(KC_E_AND, node->visibility,
					  kc_expr_copy(parent_vis));
		else
			node->visibility = kc_expr_copy(parent_vis);

		if (node->sym) {
			struct kc_property *prop;

			for (prop = node->sym->props; prop; prop = prop->next) {
				if (prop->kind != KC_PROP_PROMPT &&
				    prop->kind != KC_PROP_DEFAULT)
					continue;
				if (prop->cond)
					prop->cond = kc_expr_alloc(
					    KC_E_AND, prop->cond,
					    kc_expr_copy(parent_vis));
				else
					prop->cond = kc_expr_copy(parent_vis);
			}
		}
	}

	for (child = node->child; child; child = child->next)
		propagate_visibility(child, node->visibility);
}

/* ------------------------------------------------------------------ */
/*  Finalization — select / imply propagation                          */
/* ------------------------------------------------------------------ */

static void propagate_select_imply(struct kc_symbol *sym)
{
	struct kc_property *prop;

	for (prop = sym->props; prop; prop = prop->next) {
		struct kc_symbol *target;
		struct kc_expr *dep;

		if (prop->kind != KC_PROP_SELECT && prop->kind != KC_PROP_IMPLY)
			continue;

		target = prop->value->data.sym;
		dep = kc_expr_alloc_sym(sym);

		if (prop->cond)
			dep = kc_expr_alloc(KC_E_AND, dep,
					    kc_expr_copy(prop->cond));

		if (prop->kind == KC_PROP_SELECT) {
			if (target->rev_deps)
				target->rev_deps = kc_expr_alloc(
				    KC_E_OR, target->rev_deps, dep);
			else
				target->rev_deps = dep;
		} else {
			if (target->weak_rev_deps)
				target->weak_rev_deps = kc_expr_alloc(
				    KC_E_OR, target->weak_rev_deps, dep);
			else
				target->weak_rev_deps = dep;
		}
	}
}

static void propagate_deps(struct kc_menu_node *node)
{
	struct kc_menu_node *child;

	if (node->sym)
		propagate_select_imply(node->sym);

	for (child = node->child; child; child = child->next)
		propagate_deps(child);
}

/* ------------------------------------------------------------------ */
/*  Public finalization entry point                                    */
/* ------------------------------------------------------------------ */

void kc_finalize(struct kc_menu_node *root, struct kc_symtab *tab)
{
	int idx;

	propagate_visibility(root, NULL);
	propagate_deps(root);

	for (idx = 0; idx < KC_SYMTAB_BUCKETS; idx++) {
		struct kc_symbol *sym;

		for (sym = tab->buckets[idx]; sym; sym = sym->hash_next)
			kc_sym_calc_value(sym);
	}
}

/* ------------------------------------------------------------------ */
/*  Menu visibility                                                    */
/* ------------------------------------------------------------------ */

int kc_menu_visible(const struct kc_menu_node *node)
{
	if (!node)
		return 0;
	if (node->visibility && kc_expr_eval(node->visibility) != KC_VAL_Y)
		return 0;
	return 1;
}
