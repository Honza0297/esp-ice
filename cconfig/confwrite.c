/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file confwrite.c
 * @brief Write an sdkconfig file from the evaluated Kconfig tree.
 *
 * Generates the same output as kconfgen --output config: a banner
 * followed by a depth-first walk that emits CONFIG_FOO=value lines,
 * comment and menu section headers, and "# end of" footers.
 */
#include "cconfig/cconfig.h"
#include "ice.h"

static int sym_has_visible_prompt(const struct kc_symbol *sym)
{
	const struct kc_property *prop;

	for (prop = sym->props; prop; prop = prop->next) {
		if (prop->kind == KC_PROP_PROMPT &&
		    (!prop->cond || kc_expr_eval(prop->cond) == KC_VAL_Y))
			return 1;
	}
	return 0;
}

/*
 * Approximation of kconfgen's has_active_default_value().
 * Promptless symbols always have an active default; symbols with a
 * prompt use the KC_SYM_CHANGED flag as a proxy.
 *
 * This may diverge from kconfgen when:
 *  - select/imply forces a value (overrides the default silently),
 *  - a choice group selects a member (choice-selected != changed),
 *  - conditional defaults evaluate differently under nested visibility.
 *
 * TODO(T10): replace the CHANGED-flag proxy with full default-chain
 *            evaluation covering the cases above.
 */
static int has_active_default(const struct kc_symbol *sym)
{
	const struct kc_property *prop;

	for (prop = sym->props; prop; prop = prop->next) {
		if (prop->kind == KC_PROP_PROMPT)
			return !(sym->flags & KC_SYM_CHANGED);
	}
	return 1;
}

static void clear_visited(struct kc_symtab *tab)
{
	int idx;

	for (idx = 0; idx < KC_SYMTAB_BUCKETS; idx++) {
		struct kc_symbol *sym;

		for (sym = tab->buckets[idx]; sym; sym = sym->hash_next)
			sym->flags &= ~CONFWRITE_VISITED;
	}
}

static void emit_banner(struct sbuf *out, const char *idf_version)
{
	const char *ver = (idf_version && *idf_version) ? idf_version : "";

	sbuf_addstr(out, "#\n");
	sbuf_addstr(out, "# Automatically generated file. DO NOT EDIT.\n");
	sbuf_addf(out,
		  "# Espressif IoT Development Framework (ESP-IDF) %s"
		  " Project Configuration\n",
		  ver);
	sbuf_addstr(out, "#\n");
}

/**
 * Emit the config-string for a single symbol: an optional
 * "# default:" pragma followed by the CONFIG_NAME=value line.
 */
static void emit_sym_config(struct sbuf *out, const struct kc_symbol *sym)
{
	const char *value = kc_sym_get_string(sym);

	if (has_active_default(sym))
		sbuf_addstr(out, "# default:\n");

	switch (sym->type) {
	case KC_TYPE_BOOL:
		if (strcmp(value, "y") == 0)
			sbuf_addf(out, "CONFIG_%s=y\n", sym->name);
		else
			sbuf_addf(out, "# CONFIG_%s is not set\n", sym->name);
		break;
	case KC_TYPE_INT:
	case KC_TYPE_HEX:
	case KC_TYPE_FLOAT:
		sbuf_addf(out, "CONFIG_%s=%s\n", sym->name, value);
		break;
	case KC_TYPE_STRING: {
		const char *ch;

		sbuf_addf(out, "CONFIG_%s=\"", sym->name);
		for (ch = value; *ch; ch++) {
			if (*ch == '\\' || *ch == '"')
				sbuf_addch(out, '\\');
			sbuf_addch(out, *ch);
		}
		sbuf_addstr(out, "\"\n");
		break;
	}
	default:
		/* KC_TYPE_UNKNOWN symbols are skipped (tristate is not
		 * supported in this project — removed in T01). */
		break;
	}
}

/**
 * A "menu" entry in Kconfig: sym==NULL, has a prompt, and has children.
 * Comment entries also have sym==NULL and a prompt but no children.
 * The root (mainmenu) node is excluded from "end of" treatment by
 * the caller (the walker never considers the root as a leaving node).
 */
static int is_menu_entry(const struct kc_menu_node *node)
{
	return !node->sym && node->prompt && node->child;
}

void kc_config_contents(struct sbuf *out, const struct kc_menu_node *root,
			struct kc_symtab *tab, const char *idf_version)
{
	const struct kc_menu_node *node;
	int after_end_comment = 0;

	clear_visited(tab);
	emit_banner(out, idf_version);

	node = root->child;
	if (!node)
		goto out;

	for (;;) {
		if (node->sym) {
			struct kc_symbol *sym = node->sym;

			if (!(sym->flags & CONFWRITE_VISITED) &&
			    !(sym->flags & KC_SYM_CHOICE) &&
			    !(sym->flags & KC_SYM_CONST) && sym->name &&
			    sym->type != KC_TYPE_UNKNOWN) {
				int should_write;

				sym->flags |= CONFWRITE_VISITED;
				should_write =
				    sym_has_visible_prompt(sym) ||
				    sym->type != KC_TYPE_BOOL ||
				    strcmp(kc_sym_get_string(sym), "y") == 0;

				if (should_write) {
					if (after_end_comment) {
						sbuf_addch(out, '\n');
						after_end_comment = 0;
					}
					emit_sym_config(out, sym);
				}
			}
		} else if (node->prompt && kc_menu_visible(node)) {
			sbuf_addf(out, "\n#\n# %s\n#\n", node->prompt);
			after_end_comment = 0;
		}

		if (node->child) {
			node = node->child;
			continue;
		}

		if (node->next) {
			node = node->next;
			continue;
		}

		for (;;) {
			node = node->parent;
			if (node == root)
				goto out;

			if (is_menu_entry(node) && kc_menu_visible(node)) {
				sbuf_addf(out, "# end of %s\n", node->prompt);
				after_end_comment = 1;
			}

			if (node->next) {
				node = node->next;
				break;
			}
		}
	}

out:
	clear_visited(tab);
}

int kc_write_config(const char *path, const struct kc_menu_node *root,
		    struct kc_symtab *tab, const char *idf_version)
{
	struct sbuf contents = SBUF_INIT;
	int rc;

	kc_config_contents(&contents, root, tab, idf_version);
	rc = write_file_atomic(path, contents.buf, contents.len);
	sbuf_release(&contents);
	return rc;
}
