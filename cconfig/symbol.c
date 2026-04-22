/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file symbol.c
 * @brief Symbol table implementation for the Kconfig processor.
 */
#include "ice.h"
#include "cconfig/cconfig.h"

struct kc_symbol *kc_sym_yes;
struct kc_symbol *kc_sym_no;

/**
 * FNV-1a hash for NUL-terminated strings.
 */
static uint32_t fnv1a(const char *str)
{
	uint32_t hash = 2166136261u;
	const unsigned char *ptr = (const unsigned char *)str;

	while (*ptr) {
		hash ^= *ptr++;
		hash *= 16777619u;
	}
	return hash;
}

static struct kc_symbol *sym_alloc(const char *name)
{
	struct kc_symbol *sym = xcalloc(1, sizeof(*sym));
	sym->name = sbuf_strdup(name);
	return sym;
}

static void sym_free(struct kc_symbol *sym)
{
	struct kc_property *prop = sym->props;

	while (prop) {
		struct kc_property *next = prop->next;
		kc_expr_free(prop->value);
		kc_expr_free(prop->cond);
		free(prop);
		prop = next;
	}

	kc_expr_free(sym->rev_deps);
	kc_expr_free(sym->weak_rev_deps);
	free(sym->curr_value);
	free((char *)sym->name);
	free(sym);
}

void kc_symtab_init(struct kc_symtab *tab)
{
	memset(tab, 0, sizeof(*tab));

	kc_sym_yes = kc_symtab_intern(tab, "y");
	kc_sym_yes->type = KC_TYPE_BOOL;
	kc_sym_yes->flags |= KC_SYM_CONST;
	kc_sym_yes->curr_value = sbuf_strdup("y");

	kc_sym_no = kc_symtab_intern(tab, "n");
	kc_sym_no->type = KC_TYPE_BOOL;
	kc_sym_no->flags |= KC_SYM_CONST;
	kc_sym_no->curr_value = sbuf_strdup("n");
}

void kc_symtab_release(struct kc_symtab *tab)
{
	int idx;

	for (idx = 0; idx < KC_SYMTAB_BUCKETS; idx++) {
		struct kc_symbol *sym = tab->buckets[idx];
		while (sym) {
			struct kc_symbol *next = sym->hash_next;
			sym_free(sym);
			sym = next;
		}
		tab->buckets[idx] = NULL;
	}

	{
		struct kc_intern_str *istr = tab->interned_strings;
		while (istr) {
			struct kc_intern_str *next = istr->next;
			free(istr->str);
			free(istr);
			istr = next;
		}
		tab->interned_strings = NULL;
	}

	kc_preproc_release(tab);

	kc_sym_yes = NULL;
	kc_sym_no = NULL;
}

const char *kc_symtab_intern_string(struct kc_symtab *tab, const char *str)
{
	struct kc_intern_str *node = xcalloc(1, sizeof(*node));

	node->str = sbuf_strdup(str);
	node->next = tab->interned_strings;
	tab->interned_strings = node;
	return node->str;
}

struct kc_symbol *kc_symtab_lookup(const struct kc_symtab *tab,
				   const char *name)
{
	uint32_t bucket = fnv1a(name) % KC_SYMTAB_BUCKETS;
	struct kc_symbol *sym;

	for (sym = tab->buckets[bucket]; sym; sym = sym->hash_next) {
		if (strcmp(sym->name, name) == 0)
			return sym;
	}
	return NULL;
}

struct kc_symbol *kc_symtab_intern(struct kc_symtab *tab, const char *name)
{
	uint32_t bucket = fnv1a(name) % KC_SYMTAB_BUCKETS;
	struct kc_symbol *sym;

	for (sym = tab->buckets[bucket]; sym; sym = sym->hash_next) {
		if (strcmp(sym->name, name) == 0)
			return sym;
	}

	sym = sym_alloc(name);
	sym->hash_next = tab->buckets[bucket];
	tab->buckets[bucket] = sym;
	return sym;
}

struct kc_property *kc_sym_add_prop(struct kc_symbol *sym,
				    enum kc_prop_kind kind)
{
	struct kc_property *prop = xcalloc(1, sizeof(*prop));

	prop->kind = kind;

	if (sym->props_tail)
		sym->props_tail->next = prop;
	else
		sym->props = prop;
	sym->props_tail = prop;

	return prop;
}

const char *kc_sym_type_name(enum kc_sym_type type)
{
	switch (type) {
	case KC_TYPE_UNKNOWN:  return "unknown";
	case KC_TYPE_BOOL:     return "bool";
	case KC_TYPE_INT:      return "int";
	case KC_TYPE_HEX:      return "hex";
	case KC_TYPE_STRING:   return "string";
	case KC_TYPE_FLOAT:    return "float";
	}
	return "unknown";
}
