/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cconfig.h
 * @brief Core data structures for the Kconfig processor.
 *
 * Defines symbols, expressions, properties, menu nodes, the
 * symbol table, and the lexer used throughout the cconfig subsystem.
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
 *
 * KC_E_RANGE stores (lo, hi) as left/right children (both KC_E_SYMBOL).
 * It is only used in KC_PROP_RANGE properties, not in logical expressions.
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
	KC_E_SYMBOL,
	KC_E_RANGE
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
#define KC_SOURCE_DEPTH_MAX 64

struct kc_intern_str {
	char *str;
	struct kc_intern_str *next;
};

struct kc_variable {
	char *name;
	char *value;
	int is_immediate;               /* := (1) vs = (0) */
	struct kc_variable *next;
};

struct kc_symtab {
	struct kc_symbol *buckets[KC_SYMTAB_BUCKETS];
	struct kc_intern_str *interned_strings;
	struct kc_variable *variables;   /* Kconfig macro variable list */
	unsigned int choice_counter;
	int source_depth;
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
const char *kc_symtab_intern_string(struct kc_symtab *tab, const char *str);

/* ------------------------------------------------------------------
 *  Constant symbols (pre-interned on symtab init)
 * ------------------------------------------------------------------ */

extern struct kc_symbol *kc_sym_yes;
extern struct kc_symbol *kc_sym_no;

/* ------------------------------------------------------------------
 *  Token types
 * ------------------------------------------------------------------ */

enum kc_token_type {
	/* Structural */
	KC_TOK_EOF,
	KC_TOK_NEWLINE,

	/* Literals */
	KC_TOK_WORD,            /* identifier or unquoted word */
	KC_TOK_QUOTED,          /* quoted string literal (unescaped content) */
	KC_TOK_HELP_TEXT,       /* help text block */

	/* Keywords */
	KC_TOK_MAINMENU,
	KC_TOK_MENU,
	KC_TOK_ENDMENU,
	KC_TOK_CONFIG,
	KC_TOK_MENUCONFIG,
	KC_TOK_CHOICE,
	KC_TOK_ENDCHOICE,
	KC_TOK_IF,
	KC_TOK_ENDIF,
	KC_TOK_COMMENT,
	KC_TOK_SOURCE,
	KC_TOK_RSOURCE,
	KC_TOK_OSOURCE,
	KC_TOK_ORSOURCE,
	KC_TOK_DEPENDS,
	KC_TOK_ON,
	KC_TOK_DEFAULT,
	KC_TOK_SELECT,
	KC_TOK_IMPLY,
	KC_TOK_RANGE,
	KC_TOK_BOOL,
	KC_TOK_INT,
	KC_TOK_STRING,
	KC_TOK_HEX,
	KC_TOK_FLOAT,
	KC_TOK_TRISTATE,
	KC_TOK_PROMPT,
	KC_TOK_HELP,
	KC_TOK_VISIBLE,
	KC_TOK_OPTIONAL,
	KC_TOK_SET,
	KC_TOK_WARNING,
	KC_TOK_DEF_BOOL,
	KC_TOK_DEF_INT,
	KC_TOK_DEF_HEX,
	KC_TOK_DEF_STRING,
	KC_TOK_DEF_TRISTATE,
	KC_TOK_DEF_FLOAT,

	/* Operators */
	KC_TOK_ASSIGN,          /* = */
	KC_TOK_COLON_ASSIGN,    /* := */
	KC_TOK_NOT_EQUAL,       /* != */
	KC_TOK_LESS,            /* < */
	KC_TOK_GREATER,         /* > */
	KC_TOK_LESS_EQ,         /* <= */
	KC_TOK_GREATER_EQ,      /* >= */
	KC_TOK_AND,             /* && */
	KC_TOK_OR,              /* || */
	KC_TOK_NOT,             /* ! */
	KC_TOK_LPAREN,          /* ( */
	KC_TOK_RPAREN,          /* ) */

	/* Error */
	KC_TOK_ERROR            /* lexer error (unterminated string, bad char) */
};

/* ------------------------------------------------------------------
 *  Lexer
 * ------------------------------------------------------------------ */

struct kc_lexer {
	const char *input;      /* full input buffer (NUL-terminated) */
	const char *pos;        /* current scan position */
	const char *file;       /* filename for error reporting */
	int line;               /* current line number (1-based) */
	int col;                /* current column (1-based) */
	int help_mode;          /* set after seeing 'help' keyword */
};

struct kc_token {
	enum kc_token_type type;
	const char *value;      /* token text (NUL-terminated, owned) */
	const char *file;       /* source file (borrowed from lexer) */
	int line;               /* line where token starts */
	int col;                /* column where token starts */
};

void kc_lexer_init(struct kc_lexer *lex, const char *input, const char *file);
void kc_lexer_next(struct kc_lexer *lex, struct kc_token *tok);
void kc_token_release(struct kc_token *tok);
const char *kc_token_type_name(enum kc_token_type type);

/* ------------------------------------------------------------------
 *  Parser
 *
 *  Ownership: the parser borrows @c filename (and the input buffer for
 *  kc_parse_buffer) only during parsing.  After return, file/line
 *  pointers inside menu nodes and properties alias the interned symbol
 *  names in @c tab or the filename string.  Callers must keep @c tab
 *  and the filename string alive while the menu tree is in use.
 *  kc_parse_file strdup's the path internally, so it is self-contained.
 *
 *  Menu-level depends-on is stored in the menu node's visibility only.
 *  Propagation to child entries happens at evaluation time via the
 *  visibility tree walk, not during parsing.
 * ------------------------------------------------------------------ */

struct kc_menu_node *kc_parse_buffer(const char *buf, const char *filename,
				     struct kc_symtab *tab);
struct kc_menu_node *kc_parse_file(const char *path, struct kc_symtab *tab);
void kc_menu_free(struct kc_menu_node *root);

/* ------------------------------------------------------------------
 *  Preprocessor (macro/variable expansion)
 *
 *  kc_preproc_set records a variable in the symtab's variable list.
 *  kc_preproc_expand expands all $(NAME) references in a string,
 *  consulting the variable table first and falling back to getenv().
 *  Undefined variables expand to the empty string.
 * ------------------------------------------------------------------ */

void kc_preproc_set(struct kc_symtab *tab, const char *name,
		    const char *value, int is_immediate);
char *kc_preproc_expand(const struct kc_symtab *tab, const char *raw);
void kc_preproc_release(struct kc_symtab *tab);

#endif /* CCONFIG_H */
