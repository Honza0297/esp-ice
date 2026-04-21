/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cconfig.h
 * @brief Core data structures for the Kconfig processor.
 *
 * Defines symbols, expressions, properties, menu nodes, and the
 * symbol table used throughout the cconfig subsystem.
 */
#ifndef CCONFIG_H
#define CCONFIG_H

#include <stddef.h>
#include <stdint.h>

struct sbuf;

/* ------------------------------------------------------------------
 *  Symbol types
 * ------------------------------------------------------------------ */

enum kc_sym_type {
	KC_TYPE_UNKNOWN,
	KC_TYPE_BOOL,
	KC_TYPE_INT,
	KC_TYPE_HEX,
	KC_TYPE_STRING,
	KC_TYPE_FLOAT
};

/* ------------------------------------------------------------------
 *  Symbol flags
 * ------------------------------------------------------------------ */

#define KC_SYM_CHOICE    (1 << 0)   /* symbol is a choice group */
#define KC_SYM_CHOICEVAL (1 << 1)   /* symbol is a member of a choice */
#define KC_SYM_CONST     (1 << 2)   /* constant symbol (y/n) */
#define KC_SYM_AUTO      (1 << 3)   /* automatically created by parser */
#define KC_SYM_VALID     (1 << 4)   /* value has been computed */

/* ------------------------------------------------------------------
 *  Expression AST
 * ------------------------------------------------------------------ */

/*
 * KC_E_NOT is a unary operator: it uses data.children.left for the
 * operand and data.children.right is always NULL.
 */
enum kc_expr_type {
	KC_E_NONE,
	KC_E_AND,
	KC_E_OR,
	KC_E_NOT,
	KC_E_EQUAL,
	KC_E_NOT_EQUAL,
	KC_E_LT,
	KC_E_GT,
	KC_E_LTE,
	KC_E_GTE,
	KC_E_SYMBOL
};

struct kc_symbol;

struct kc_expr {
	enum kc_expr_type type;
	union {
		struct {
			struct kc_expr *left;
			struct kc_expr *right;
		} children;
		struct kc_symbol *sym;
	} data;
};

struct kc_expr *kc_expr_alloc(enum kc_expr_type type,
			      struct kc_expr *left,
			      struct kc_expr *right);
struct kc_expr *kc_expr_alloc_sym(struct kc_symbol *sym);
struct kc_expr *kc_expr_alloc_comp(enum kc_expr_type type,
				   struct kc_symbol *sym_left,
				   struct kc_symbol *sym_right);
void kc_expr_free(struct kc_expr *expr);
void kc_expr_print(const struct kc_expr *expr, struct sbuf *sb);

/* ------------------------------------------------------------------
 *  Property (attached to a symbol)
 * ------------------------------------------------------------------ */

enum kc_prop_kind {
	KC_PROP_DEFAULT,
	KC_PROP_PROMPT,
	KC_PROP_SELECT,
	KC_PROP_IMPLY,
	KC_PROP_RANGE,
	KC_PROP_SET,
	KC_PROP_SET_DEFAULT
};

struct kc_property {
	enum kc_prop_kind kind;
	struct kc_expr *value;
	struct kc_expr *cond;
	const char *file;
	int line;
	struct kc_property *next;
};

/* ------------------------------------------------------------------
 *  Symbol
 * ------------------------------------------------------------------ */

struct kc_menu_node;

struct kc_symbol {
	const char *name;
	enum kc_sym_type type;
	char *curr_value;
	unsigned int flags;
	struct kc_property *props;
	struct kc_property *props_tail;
	struct kc_expr *rev_deps;
	struct kc_expr *weak_rev_deps;
	struct kc_menu_node *menu_node;
	struct kc_symbol *hash_next;
};

/* ------------------------------------------------------------------
 *  Menu tree node
 * ------------------------------------------------------------------ */

struct kc_menu_node {
	struct kc_menu_node *parent;
	struct kc_menu_node *child;
	struct kc_menu_node *next;
	struct kc_symbol *sym;
	const char *prompt;
	struct kc_expr *visibility;
	char *help;
	const char *file;
	int line;
	int is_menuconfig;  /* boolean: 1 if menuconfig entry, 0 otherwise */
};

/* ------------------------------------------------------------------
 *  Symbol table (hash-based)
 *
 *  Only one kc_symtab may be live at a time: kc_sym_yes and kc_sym_no
 *  are process-wide pointers updated by kc_symtab_init/release.
 * ------------------------------------------------------------------ */

#define KC_SYMTAB_BUCKETS 256

struct kc_symtab {
	struct kc_symbol *buckets[KC_SYMTAB_BUCKETS];
};

void kc_symtab_init(struct kc_symtab *tab);
void kc_symtab_release(struct kc_symtab *tab);
struct kc_symbol *kc_symtab_lookup(const struct kc_symtab *tab,
				   const char *name);
struct kc_symbol *kc_symtab_intern(struct kc_symtab *tab, const char *name);

/* ------------------------------------------------------------------
 *  Symbol helpers
 * ------------------------------------------------------------------ */

struct kc_property *kc_sym_add_prop(struct kc_symbol *sym,
				    enum kc_prop_kind kind);
const char *kc_sym_type_name(enum kc_sym_type type);

/* ------------------------------------------------------------------
 *  Constant symbols (pre-interned on symtab init)
 * ------------------------------------------------------------------ */

extern struct kc_symbol *kc_sym_yes;
extern struct kc_symbol *kc_sym_no;

#endif /* CCONFIG_H */
