/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file parser.c
 * @brief Recursive descent parser for Kconfig structural elements.
 *
 * Parses mainmenu, config, menuconfig, menu/endmenu, choice/endchoice,
 * comment, if/endif, and source/rsource/osource/orsource entries from
 * a token stream produced by the lexer.  Builds a menu tree and
 * populates symbol properties.  Expression parsing follows standard
 * precedence:
 *   || (lowest) > && > ! (prefix) > atoms/comparisons (highest).
 */
#include "ice.h"
#include "cconfig/cconfig.h"

/* ------------------------------------------------------------------ */
/*  Parser context                                                     */
/* ------------------------------------------------------------------ */

struct parser {
	struct kc_lexer lex;
	struct kc_token cur;
	struct kc_symtab *tab;
	struct kc_menu_node *root;
	const char *filename;
};

static struct kc_expr *parse_expr(struct parser *p);
static void parse_block(struct parser *p, struct kc_menu_node *parent);

/* ------------------------------------------------------------------ */
/*  Token helpers                                                      */
/* ------------------------------------------------------------------ */

static void advance(struct parser *p)
{
	kc_token_release(&p->cur);
	kc_lexer_next(&p->lex, &p->cur);

	/* Expand $(NAME) references in quoted string tokens. */
	if (p->cur.type == KC_TOK_QUOTED &&
	    p->cur.value && strchr(p->cur.value, '$')) {
		char *expanded = kc_preproc_expand(p->tab, p->cur.value);
		free((void *)p->cur.value);
		p->cur.value = expanded;
	}
}

static void skip_newlines(struct parser *p)
{
	while (p->cur.type == KC_TOK_NEWLINE)
		advance(p);
}

static void expect_newline(struct parser *p)
{
	if (p->cur.type != KC_TOK_NEWLINE && p->cur.type != KC_TOK_EOF)
		die("error: %s:%d: expected newline, got %s",
		    p->filename, p->cur.line,
		    kc_token_type_name(p->cur.type));
	if (p->cur.type == KC_TOK_NEWLINE)
		advance(p);
}

/* ------------------------------------------------------------------ */
/*  Menu node allocation                                               */
/* ------------------------------------------------------------------ */

static struct kc_menu_node *alloc_node(struct kc_menu_node *parent,
				       const char *file, int line)
{
	struct kc_menu_node *node = xcalloc(1, sizeof(*node));

	node->parent = parent;
	node->file = file;
	node->line = line;

	if (parent) {
		if (!parent->child) {
			parent->child = node;
		} else {
			struct kc_menu_node *last = parent->child;
			while (last->next)
				last = last->next;
			last->next = node;
		}
	}

	return node;
}

/* ------------------------------------------------------------------ */
/*  Expression helpers                                                 */
/* ------------------------------------------------------------------ */

static struct kc_expr *expr_copy(const struct kc_expr *src)
{
	struct kc_expr *copy;

	if (!src)
		return NULL;

	copy = xcalloc(1, sizeof(*copy));
	copy->type = src->type;

	if (src->type == KC_E_SYMBOL) {
		copy->data.sym = src->data.sym;
	} else {
		copy->data.children.left = expr_copy(src->data.children.left);
		copy->data.children.right = expr_copy(src->data.children.right);
	}

	return copy;
}

/**
 * AND two expressions; takes ownership of both.
 */
static struct kc_expr *expr_and(struct kc_expr *existing, struct kc_expr *dep)
{
	if (!existing)
		return dep;
	if (!dep)
		return existing;
	return kc_expr_alloc(KC_E_AND, existing, dep);
}

/* ------------------------------------------------------------------ */
/*  Symbol parsing                                                     */
/* ------------------------------------------------------------------ */

/**
 * Consume a WORD or QUOTED token, intern its text, and advance.
 *
 * Both bare identifiers and quoted strings are interned as symbols.
 * This means `default "hello"` creates a symbol named "hello" rather
 * than a distinct string literal.  For Kconfig semantics this is
 * sufficient because string values are resolved by name at evaluation
 * time, and the evaluator treats symbols without a config entry as
 * string constants.
 */
static struct kc_symbol *parse_symbol(struct parser *p)
{
	struct kc_symbol *sym;

	if (p->cur.type != KC_TOK_WORD && p->cur.type != KC_TOK_QUOTED)
		die("error: %s:%d: expected symbol or string, got %s",
		    p->filename, p->cur.line,
		    kc_token_type_name(p->cur.type));

	sym = kc_symtab_intern(p->tab, p->cur.value);
	advance(p);
	return sym;
}

/* ------------------------------------------------------------------ */
/*  Expression parser (recursive descent, precedence climbing)         */
/* ------------------------------------------------------------------ */

static struct kc_expr *parse_atom(struct parser *p)
{
	struct kc_expr *expr;
	struct kc_symbol *sym;

	if (p->cur.type == KC_TOK_LPAREN) {
		advance(p);
		expr = parse_expr(p);
		if (p->cur.type != KC_TOK_RPAREN)
			die("error: %s:%d: expected ')', got %s",
			    p->filename, p->cur.line,
			    kc_token_type_name(p->cur.type));
		advance(p);
		return expr;
	}

	sym = parse_symbol(p);
	expr = kc_expr_alloc_sym(sym);

	{
		enum kc_expr_type cmp = KC_E_NONE;

		switch (p->cur.type) {
		case KC_TOK_ASSIGN:     cmp = KC_E_EQUAL; break;
		case KC_TOK_NOT_EQUAL:  cmp = KC_E_NOT_EQUAL; break;
		case KC_TOK_LESS:       cmp = KC_E_LT; break;
		case KC_TOK_GREATER:    cmp = KC_E_GT; break;
		case KC_TOK_LESS_EQ:    cmp = KC_E_LTE; break;
		case KC_TOK_GREATER_EQ: cmp = KC_E_GTE; break;
		default: break;
		}

		if (cmp != KC_E_NONE) {
			struct kc_symbol *rhs;

			advance(p);
			rhs = parse_symbol(p);
			kc_expr_free(expr);
			expr = kc_expr_alloc_comp(cmp, sym, rhs);
		}
	}

	return expr;
}

static struct kc_expr *parse_unary(struct parser *p)
{
	if (p->cur.type == KC_TOK_NOT) {
		struct kc_expr *operand;

		advance(p);
		operand = parse_unary(p);
		return kc_expr_alloc(KC_E_NOT, operand, NULL);
	}
	return parse_atom(p);
}

static struct kc_expr *parse_and_expr(struct parser *p)
{
	struct kc_expr *left = parse_unary(p);

	while (p->cur.type == KC_TOK_AND) {
		struct kc_expr *right;

		advance(p);
		right = parse_unary(p);
		left = kc_expr_alloc(KC_E_AND, left, right);
	}

	return left;
}

static struct kc_expr *parse_or_expr(struct parser *p)
{
	struct kc_expr *left = parse_and_expr(p);

	while (p->cur.type == KC_TOK_OR) {
		struct kc_expr *right;

		advance(p);
		right = parse_and_expr(p);
		left = kc_expr_alloc(KC_E_OR, left, right);
	}

	return left;
}

static struct kc_expr *parse_expr(struct parser *p)
{
	return parse_or_expr(p);
}

static struct kc_expr *parse_if_cond(struct parser *p)
{
	if (p->cur.type != KC_TOK_IF)
		return NULL;
	advance(p);
	return parse_expr(p);
}

/* ------------------------------------------------------------------ */
/*  Property helpers                                                   */
/* ------------------------------------------------------------------ */

static void set_prop_loc(struct kc_property *prop, const struct parser *p,
			 int line)
{
	prop->file = p->filename;
	prop->line = line;
}

/* ------------------------------------------------------------------ */
/*  Type helpers                                                       */
/* ------------------------------------------------------------------ */

static enum kc_sym_type tok_to_type(enum kc_token_type tok)
{
	switch (tok) {
	case KC_TOK_BOOL:     case KC_TOK_DEF_BOOL:    return KC_TYPE_BOOL;
	case KC_TOK_INT:      case KC_TOK_DEF_INT:     return KC_TYPE_INT;
	case KC_TOK_HEX:      case KC_TOK_DEF_HEX:     return KC_TYPE_HEX;
	case KC_TOK_STRING:   case KC_TOK_DEF_STRING:   return KC_TYPE_STRING;
	case KC_TOK_FLOAT:    case KC_TOK_DEF_FLOAT:    return KC_TYPE_FLOAT;
	default:                                        return KC_TYPE_UNKNOWN;
	}
}

static int is_config_property(enum kc_token_type tok)
{
	switch (tok) {
	case KC_TOK_BOOL:  case KC_TOK_INT:   case KC_TOK_HEX:
	case KC_TOK_STRING: case KC_TOK_FLOAT:
	case KC_TOK_DEF_BOOL: case KC_TOK_DEF_INT: case KC_TOK_DEF_HEX:
	case KC_TOK_DEF_STRING: case KC_TOK_DEF_FLOAT:
	case KC_TOK_TRISTATE: case KC_TOK_DEF_TRISTATE:
	case KC_TOK_PROMPT: case KC_TOK_DEFAULT:
	case KC_TOK_DEPENDS: case KC_TOK_SELECT: case KC_TOK_IMPLY:
	case KC_TOK_RANGE: case KC_TOK_HELP: case KC_TOK_WARNING:
		return 1;
	default:
		return 0;
	}
}

static int is_choice_property(enum kc_token_type tok)
{
	switch (tok) {
	case KC_TOK_BOOL:  case KC_TOK_INT:   case KC_TOK_HEX:
	case KC_TOK_STRING: case KC_TOK_FLOAT:
	case KC_TOK_TRISTATE: case KC_TOK_DEF_TRISTATE:
	case KC_TOK_PROMPT: case KC_TOK_DEFAULT:
	case KC_TOK_DEPENDS: case KC_TOK_OPTIONAL:
	case KC_TOK_HELP:
		return 1;
	default:
		return 0;
	}
}

/* ------------------------------------------------------------------ */
/*  depends on inheritance                                             */
/* ------------------------------------------------------------------ */

/**
 * AND @p deps into the menu node's visibility and into every PROMPT
 * and DEFAULT condition on @p sym.  Takes ownership of @p deps.
 */
static void apply_deps(struct kc_menu_node *node, struct kc_symbol *sym,
		       struct kc_expr *deps)
{
	struct kc_property *prop;

	if (!deps)
		return;

	node->visibility = expr_and(node->visibility, expr_copy(deps));

	if (sym) {
		for (prop = sym->props; prop; prop = prop->next) {
			if (prop->kind == KC_PROP_PROMPT ||
			    prop->kind == KC_PROP_DEFAULT)
				prop->cond = expr_and(prop->cond,
						      expr_copy(deps));
		}
	}

	kc_expr_free(deps);
}

/* ------------------------------------------------------------------ */
/*  Config / menuconfig entry                                          */
/* ------------------------------------------------------------------ */

static void parse_config_inner(struct parser *p,
			       struct kc_menu_node *parent,
			       int is_menuconfig)
{
	struct kc_menu_node *node;
	struct kc_symbol *sym;
	struct kc_expr *deps = NULL;
	int entry_line = p->cur.line;

	advance(p);

	if (p->cur.type != KC_TOK_WORD)
		die("error: %s:%d: expected symbol name after '%s'",
		    p->filename, p->cur.line,
		    is_menuconfig ? "menuconfig" : "config");

	sym = kc_symtab_intern(p->tab, p->cur.value);
	advance(p);
	expect_newline(p);

	node = alloc_node(parent, p->filename, entry_line);
	node->sym = sym;
	node->is_menuconfig = is_menuconfig;
	sym->menu_node = node;

	skip_newlines(p);
	while (is_config_property(p->cur.type)) {
		int prop_line = p->cur.line;

		switch (p->cur.type) {
		case KC_TOK_TRISTATE:
		case KC_TOK_DEF_TRISTATE:
			die("error: %s:%d: tristate type is not supported",
			    p->filename, p->cur.line);
			break;

		case KC_TOK_BOOL:
		case KC_TOK_INT:
		case KC_TOK_HEX:
		case KC_TOK_STRING:
		case KC_TOK_FLOAT:
		{
			sym->type = tok_to_type(p->cur.type);
			advance(p);

			if (p->cur.type == KC_TOK_QUOTED) {
				struct kc_property *prop;
				struct kc_symbol *text;

				text = kc_symtab_intern(p->tab,
							p->cur.value);
				advance(p);

				prop = kc_sym_add_prop(sym, KC_PROP_PROMPT);
				prop->value = kc_expr_alloc_sym(text);
				set_prop_loc(prop, p, prop_line);
				prop->cond = parse_if_cond(p);

				if (!node->prompt)
					node->prompt = text->name;
			}

			expect_newline(p);
			break;
		}

		case KC_TOK_DEF_BOOL:
		case KC_TOK_DEF_INT:
		case KC_TOK_DEF_HEX:
		case KC_TOK_DEF_STRING:
		case KC_TOK_DEF_FLOAT:
		{
			struct kc_property *prop;

			sym->type = tok_to_type(p->cur.type);
			advance(p);

			prop = kc_sym_add_prop(sym, KC_PROP_DEFAULT);
			prop->value = parse_expr(p);
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);
			expect_newline(p);
			break;
		}

		case KC_TOK_PROMPT:
		{
			struct kc_property *prop;
			struct kc_symbol *text;

			advance(p);

			if (p->cur.type != KC_TOK_QUOTED)
				die("error: %s:%d: expected quoted string "
				    "after 'prompt'",
				    p->filename, p->cur.line);

			text = kc_symtab_intern(p->tab, p->cur.value);
			advance(p);

			prop = kc_sym_add_prop(sym, KC_PROP_PROMPT);
			prop->value = kc_expr_alloc_sym(text);
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);

			if (!node->prompt)
				node->prompt = text->name;

			expect_newline(p);
			break;
		}

		case KC_TOK_DEFAULT:
		{
			struct kc_property *prop;

			advance(p);

			prop = kc_sym_add_prop(sym, KC_PROP_DEFAULT);
			prop->value = parse_expr(p);
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);
			expect_newline(p);
			break;
		}

		case KC_TOK_DEPENDS:
		{
			struct kc_expr *dep;

			advance(p);
			if (p->cur.type != KC_TOK_ON)
				die("error: %s:%d: expected 'on' after "
				    "'depends'",
				    p->filename, p->cur.line);
			advance(p);

			dep = parse_expr(p);
			deps = expr_and(deps, dep);
			expect_newline(p);
			break;
		}

		case KC_TOK_SELECT:
		{
			struct kc_property *prop;

			advance(p);

			prop = kc_sym_add_prop(sym, KC_PROP_SELECT);
			prop->value = kc_expr_alloc_sym(parse_symbol(p));
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);
			expect_newline(p);
			break;
		}

		case KC_TOK_IMPLY:
		{
			struct kc_property *prop;

			advance(p);

			prop = kc_sym_add_prop(sym, KC_PROP_IMPLY);
			prop->value = kc_expr_alloc_sym(parse_symbol(p));
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);
			expect_newline(p);
			break;
		}

		case KC_TOK_RANGE:
		{
			struct kc_property *prop;
			struct kc_symbol *lo_sym, *hi_sym;

			advance(p);

			lo_sym = parse_symbol(p);
			hi_sym = parse_symbol(p);

			prop = kc_sym_add_prop(sym, KC_PROP_RANGE);
			prop->value = kc_expr_alloc(KC_E_RANGE,
					kc_expr_alloc_sym(lo_sym),
					kc_expr_alloc_sym(hi_sym));
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);
			expect_newline(p);
			break;
		}

		case KC_TOK_HELP:
			advance(p);
			if (p->cur.type == KC_TOK_HELP_TEXT) {
				node->help = sbuf_strdup(p->cur.value);
				advance(p);
			}
			break;

		case KC_TOK_WARNING:
		{
			/* TODO: wire KC_PROP_WARNING once the report subsystem lands (T09) */
			struct kc_expr *warn_cond;

			advance(p);
			if (p->cur.type == KC_TOK_QUOTED)
				advance(p);
			warn_cond = parse_if_cond(p);
			kc_expr_free(warn_cond);
			expect_newline(p);
			break;
		}

		default:
			die("BUG: unhandled config property token %s",
			    kc_token_type_name(p->cur.type));
		}

		skip_newlines(p);
	}

	apply_deps(node, sym, deps);
}

static void parse_config(struct parser *p, struct kc_menu_node *parent)
{
	parse_config_inner(p, parent, 0);
}

static void parse_menuconfig(struct parser *p, struct kc_menu_node *parent)
{
	parse_config_inner(p, parent, 1);
}

/* ------------------------------------------------------------------ */
/*  Choice block                                                       */
/* ------------------------------------------------------------------ */

/**
 * Recursively mark all config symbols under a choice node with
 * KC_SYM_CHOICEVAL.  Descends into sym-less nodes (e.g. if-blocks)
 * so that conditional choice members are properly flagged.
 */
static void mark_choice_members(struct kc_menu_node *node)
{
	struct kc_menu_node *child;

	for (child = node->child; child; child = child->next) {
		if (child->sym)
			child->sym->flags |= KC_SYM_CHOICEVAL;
		if (!child->sym && child->child)
			mark_choice_members(child);
	}
}

static void parse_choice(struct parser *p, struct kc_menu_node *parent)
{
	struct kc_menu_node *node;
	struct kc_symbol *choice_sym;
	struct kc_expr *deps = NULL;
	int entry_line = p->cur.line;
	char name_buf[64];

	advance(p);
	expect_newline(p);

	snprintf(name_buf, sizeof(name_buf), "<choice_%u>",
		 p->tab->choice_counter++);
	choice_sym = kc_symtab_intern(p->tab, name_buf);
	choice_sym->flags |= KC_SYM_CHOICE;

	node = alloc_node(parent, p->filename, entry_line);
	node->sym = choice_sym;
	choice_sym->menu_node = node;

	skip_newlines(p);
	while (is_choice_property(p->cur.type)) {
		int prop_line = p->cur.line;

		switch (p->cur.type) {
		case KC_TOK_TRISTATE:
		case KC_TOK_DEF_TRISTATE:
			die("error: %s:%d: tristate type is not supported",
			    p->filename, p->cur.line);
			break;

		case KC_TOK_BOOL:
		case KC_TOK_INT:
		case KC_TOK_HEX:
		case KC_TOK_STRING:
		case KC_TOK_FLOAT:
		{
			choice_sym->type = tok_to_type(p->cur.type);
			advance(p);

			if (p->cur.type == KC_TOK_QUOTED) {
				struct kc_property *prop;
				struct kc_symbol *text;

				text = kc_symtab_intern(p->tab,
							p->cur.value);
				advance(p);

				prop = kc_sym_add_prop(choice_sym,
						       KC_PROP_PROMPT);
				prop->value = kc_expr_alloc_sym(text);
				set_prop_loc(prop, p, prop_line);
				prop->cond = parse_if_cond(p);

				if (!node->prompt)
					node->prompt = text->name;
			}

			expect_newline(p);
			break;
		}

		case KC_TOK_PROMPT:
		{
			struct kc_property *prop;
			struct kc_symbol *text;

			advance(p);

			if (p->cur.type != KC_TOK_QUOTED)
				die("error: %s:%d: expected quoted string "
				    "after 'prompt'",
				    p->filename, p->cur.line);

			text = kc_symtab_intern(p->tab, p->cur.value);
			advance(p);

			prop = kc_sym_add_prop(choice_sym, KC_PROP_PROMPT);
			prop->value = kc_expr_alloc_sym(text);
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);

			if (!node->prompt)
				node->prompt = text->name;

			expect_newline(p);
			break;
		}

		case KC_TOK_DEFAULT:
		{
			struct kc_property *prop;

			advance(p);

			prop = kc_sym_add_prop(choice_sym, KC_PROP_DEFAULT);
			prop->value = kc_expr_alloc_sym(parse_symbol(p));
			set_prop_loc(prop, p, prop_line);
			prop->cond = parse_if_cond(p);
			expect_newline(p);
			break;
		}

		case KC_TOK_DEPENDS:
		{
			struct kc_expr *dep;

			advance(p);
			if (p->cur.type != KC_TOK_ON)
				die("error: %s:%d: expected 'on' after "
				    "'depends'",
				    p->filename, p->cur.line);
			advance(p);

			dep = parse_expr(p);
			deps = expr_and(deps, dep);
			expect_newline(p);
			break;
		}

		case KC_TOK_OPTIONAL:
			/* TODO: store optional flag on choice symbol once evaluator needs it */
			advance(p);
			expect_newline(p);
			break;

		case KC_TOK_HELP:
			advance(p);
			if (p->cur.type == KC_TOK_HELP_TEXT) {
				node->help = sbuf_strdup(p->cur.value);
				advance(p);
			}
			break;

		default:
			die("BUG: unhandled choice property token %s",
			    kc_token_type_name(p->cur.type));
		}

		skip_newlines(p);
	}

	if (deps)
		node->visibility = expr_and(node->visibility, deps);

	parse_block(p, node);

	mark_choice_members(node);

	if (p->cur.type != KC_TOK_ENDCHOICE)
		die("error: %s:%d: expected 'endchoice'",
		    p->filename, p->cur.line);
	advance(p);
	expect_newline(p);
}

/* ------------------------------------------------------------------ */
/*  if/endif block                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Parse an if/endif conditional block.
 *
 * Creates a transparent menu node (no symbol, no prompt) that stores
 * the condition as its visibility.  Propagation of the if-condition
 * to child entries happens at evaluation time via the visibility tree
 * walk, not during parsing.
 */
static void parse_if_block(struct parser *p, struct kc_menu_node *parent)
{
	struct kc_menu_node *node;
	int entry_line = p->cur.line;

	advance(p);

	node = alloc_node(parent, p->filename, entry_line);
	node->visibility = parse_expr(p);
	expect_newline(p);

	parse_block(p, node);

	if (p->cur.type != KC_TOK_ENDIF)
		die("error: %s:%d: expected 'endif'",
		    p->filename, p->cur.line);
	advance(p);
	expect_newline(p);
}

/* ------------------------------------------------------------------ */
/*  source/rsource/osource/orsource                                    */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  Variable assignment: WORD = value | WORD := value                  */
/* ------------------------------------------------------------------ */

/**
 * Parse the rest-of-line after the assignment operator as the variable
 * value.  Tokens are concatenated without separators, which works for
 * the common `$(NAME)` pattern (lexed as separate `$`/`(`/name/`)`
 * tokens).  Multi-word values like `FOO = hello world` lose the space;
 * this is a known limitation — Kconfig macro values rarely contain
 * literal whitespace.
 */
static void parse_variable_assignment(struct parser *p, const char *name,
				      int is_immediate)
{
	struct sbuf val = SBUF_INIT;

	while (p->cur.type != KC_TOK_NEWLINE && p->cur.type != KC_TOK_EOF) {
		if (p->cur.value)
			sbuf_addstr(&val, p->cur.value);
		advance(p);
	}

	kc_preproc_set(p->tab, name, val.buf, is_immediate);
	sbuf_release(&val);

	if (p->cur.type == KC_TOK_NEWLINE)
		advance(p);
}

static void parse_source(struct parser *p, struct kc_menu_node *parent,
			  int is_relative, int is_optional)
{
	struct kc_menu_node *included;
	char *expanded;
	char *resolved = NULL;
	const char *interned;
	int src_line = p->cur.line;

	advance(p);

	if (p->cur.type != KC_TOK_QUOTED)
		die("error: %s:%d: expected quoted file path after source "
		    "directive",
		    p->filename, p->cur.line);

	/* Already expanded by advance(); just duplicate. */
	expanded = sbuf_strdup(p->cur.value);
	advance(p);
	expect_newline(p);

	if (is_relative) {
		struct sbuf path_buf = SBUF_INIT;
		const char *slash;

		slash = strrchr(p->filename, '/');
		if (slash) {
			sbuf_add(&path_buf, p->filename,
				 (size_t)(slash - p->filename + 1));
		}
		sbuf_addstr(&path_buf, expanded);
		free(expanded);
		resolved = sbuf_detach(&path_buf);
	} else {
		resolved = expanded;
	}

	{
		struct sbuf probe = SBUF_INIT;

		if (sbuf_read_file(&probe, resolved) < 0) {
			if (is_optional) {
				free(resolved);
				sbuf_release(&probe);
				return;
			}
			die("error: %s:%d: cannot read source file '%s'",
			    p->filename, src_line, resolved);
		}

		if (p->tab->source_depth >= KC_SOURCE_DEPTH_MAX)
			die("error: %s:%d: source inclusion depth exceeds "
			    "%d (cycle?)",
			    p->filename, src_line, KC_SOURCE_DEPTH_MAX);

		p->tab->source_depth++;
		interned = kc_symtab_intern_string(p->tab, resolved);
		free(resolved);
		included = kc_parse_buffer(probe.buf, interned, p->tab);
		p->tab->source_depth--;
		sbuf_release(&probe);
	}

	if (included->prompt)
		die("error: %s:%d: included file '%s' contains a "
		    "mainmenu directive (only allowed in root file)",
		    p->filename, src_line, interned);

	/* Graft included children into the current tree. */
	{
		struct kc_menu_node *child = included->child;
		struct kc_menu_node *tail = NULL;

		if (parent->child) {
			tail = parent->child;
			while (tail->next)
				tail = tail->next;
		}

		while (child) {
			struct kc_menu_node *next_sib = child->next;

			child->parent = parent;
			child->next = NULL;

			if (!tail) {
				parent->child = child;
			} else {
				tail->next = child;
			}
			tail = child;

			child = next_sib;
		}
	}

	included->child = NULL;
	kc_menu_free(included);
}

/* ------------------------------------------------------------------ */
/*  Menu block                                                         */
/* ------------------------------------------------------------------ */

static void parse_menu(struct parser *p, struct kc_menu_node *parent)
{
	struct kc_menu_node *node;
	struct kc_symbol *title_sym;
	struct kc_expr *deps = NULL;
	int menu_line = p->cur.line;

	advance(p);

	if (p->cur.type != KC_TOK_QUOTED)
		die("error: %s:%d: expected quoted title after 'menu'",
		    p->filename, p->cur.line);

	title_sym = kc_symtab_intern(p->tab, p->cur.value);
	advance(p);
	expect_newline(p);

	node = alloc_node(parent, p->filename, menu_line);
	node->prompt = title_sym->name;

	skip_newlines(p);
	while (p->cur.type == KC_TOK_DEPENDS ||
	       p->cur.type == KC_TOK_VISIBLE) {
		if (p->cur.type == KC_TOK_DEPENDS) {
			struct kc_expr *dep;

			advance(p);
			if (p->cur.type != KC_TOK_ON)
				die("error: %s:%d: expected 'on' after "
				    "'depends'",
				    p->filename, p->cur.line);
			advance(p);
			dep = parse_expr(p);
			deps = expr_and(deps, dep);
			expect_newline(p);
		} else {
			advance(p);
			if (p->cur.type != KC_TOK_IF)
				die("error: %s:%d: expected 'if' after "
				    "'visible'",
				    p->filename, p->cur.line);
			advance(p);
			node->visibility = expr_and(node->visibility,
						    parse_expr(p));
			expect_newline(p);
		}
		skip_newlines(p);
	}

	if (deps)
		node->visibility = expr_and(node->visibility, deps);

	parse_block(p, node);

	if (p->cur.type != KC_TOK_ENDMENU)
		die("error: %s:%d: expected 'endmenu'",
		    p->filename, p->cur.line);
	advance(p);
	expect_newline(p);
}

/* ------------------------------------------------------------------ */
/*  Comment entry                                                      */
/* ------------------------------------------------------------------ */

static void parse_comment_entry(struct parser *p,
				struct kc_menu_node *parent)
{
	struct kc_menu_node *node;
	struct kc_symbol *text_sym;
	struct kc_expr *deps = NULL;
	int entry_line = p->cur.line;

	advance(p);

	if (p->cur.type != KC_TOK_QUOTED)
		die("error: %s:%d: expected quoted text after 'comment'",
		    p->filename, p->cur.line);

	text_sym = kc_symtab_intern(p->tab, p->cur.value);
	advance(p);
	expect_newline(p);

	node = alloc_node(parent, p->filename, entry_line);
	node->prompt = text_sym->name;

	skip_newlines(p);
	while (p->cur.type == KC_TOK_DEPENDS) {
		struct kc_expr *dep;

		advance(p);
		if (p->cur.type != KC_TOK_ON)
			die("error: %s:%d: expected 'on' after 'depends'",
			    p->filename, p->cur.line);
		advance(p);
		dep = parse_expr(p);
		deps = expr_and(deps, dep);
		expect_newline(p);
		skip_newlines(p);
	}

	if (deps)
		node->visibility = expr_and(node->visibility, deps);
}

/* ------------------------------------------------------------------ */
/*  Mainmenu                                                           */
/* ------------------------------------------------------------------ */

static void parse_mainmenu(struct parser *p)
{
	struct kc_symbol *title_sym;

	if (p->root->prompt)
		die("error: %s:%d: duplicate mainmenu "
		    "(first defined at line %d)",
		    p->filename, p->cur.line, p->root->line);

	advance(p);

	if (p->cur.type != KC_TOK_QUOTED)
		die("error: %s:%d: expected quoted title after 'mainmenu'",
		    p->filename, p->cur.line);

	title_sym = kc_symtab_intern(p->tab, p->cur.value);
	p->root->prompt = title_sym->name;
	p->root->line = p->cur.line;
	advance(p);
	expect_newline(p);
}

/* ------------------------------------------------------------------ */
/*  Block parser                                                       */
/* ------------------------------------------------------------------ */

static void parse_block(struct parser *p, struct kc_menu_node *parent)
{
	skip_newlines(p);

	while (p->cur.type != KC_TOK_EOF &&
	       p->cur.type != KC_TOK_ENDMENU &&
	       p->cur.type != KC_TOK_ENDCHOICE &&
	       p->cur.type != KC_TOK_ENDIF) {
		switch (p->cur.type) {
		case KC_TOK_MAINMENU:
			parse_mainmenu(p);
			break;
		case KC_TOK_CONFIG:
			parse_config(p, parent);
			break;
		case KC_TOK_MENUCONFIG:
			parse_menuconfig(p, parent);
			break;
		case KC_TOK_MENU:
			parse_menu(p, parent);
			break;
		case KC_TOK_COMMENT:
			parse_comment_entry(p, parent);
			break;
		case KC_TOK_CHOICE:
			parse_choice(p, parent);
			break;
		case KC_TOK_IF:
			parse_if_block(p, parent);
			break;
		case KC_TOK_SOURCE:
			parse_source(p, parent, 0, 0);
			break;
		case KC_TOK_RSOURCE:
			parse_source(p, parent, 1, 0);
			break;
		case KC_TOK_OSOURCE:
			parse_source(p, parent, 0, 1);
			break;
		case KC_TOK_ORSOURCE:
			parse_source(p, parent, 1, 1);
			break;
		case KC_TOK_WORD:
		{
			char *var_name = sbuf_strdup(p->cur.value);

			advance(p);
			if (p->cur.type == KC_TOK_ASSIGN) {
				advance(p);
				parse_variable_assignment(p, var_name, 0);
				free(var_name);
				break;
			}
			if (p->cur.type == KC_TOK_COLON_ASSIGN) {
				advance(p);
				parse_variable_assignment(p, var_name, 1);
				free(var_name);
				break;
			}
			free(var_name);
			die("error: %s:%d: unexpected token %s after "
			    "identifier",
			    p->filename, p->cur.line,
			    kc_token_type_name(p->cur.type));
			break;
		}
		default:
			die("error: %s:%d: unexpected token %s",
			    p->filename, p->cur.line,
			    kc_token_type_name(p->cur.type));
		}
		skip_newlines(p);
	}
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

struct kc_menu_node *kc_parse_buffer(const char *buf, const char *filename,
				     struct kc_symtab *tab)
{
	struct parser ctx;

	memset(&ctx, 0, sizeof(ctx));
	kc_lexer_init(&ctx.lex, buf, filename);
	ctx.tab = tab;
	ctx.filename = filename;
	ctx.root = alloc_node(NULL, filename, 0);

	kc_lexer_next(&ctx.lex, &ctx.cur);
	parse_block(&ctx, ctx.root);

	if (ctx.cur.type != KC_TOK_EOF)
		die("error: %s:%d: unexpected token %s at end of input",
		    filename, ctx.cur.line,
		    kc_token_type_name(ctx.cur.type));

	kc_token_release(&ctx.cur);
	return ctx.root;
}

struct kc_menu_node *kc_parse_file(const char *path, struct kc_symtab *tab)
{
	struct sbuf content = SBUF_INIT;
	struct kc_menu_node *root;

	if (sbuf_read_file(&content, path) < 0)
		die_errno("error: cannot read '%s'", path);

	root = kc_parse_buffer(content.buf, path, tab);
	sbuf_release(&content);
	return root;
}

void kc_menu_free(struct kc_menu_node *root)
{
	struct kc_menu_node *node = root;

	while (node) {
		struct kc_menu_node *next = node->next;

		kc_menu_free(node->child);
		kc_expr_free(node->visibility);
		free(node->help);
		free(node);
		node = next;
	}
}
