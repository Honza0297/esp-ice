/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file preproc.c
 * @brief Kconfig macro/variable preprocessor.
 *
 * Manages a variable table on the symtab and expands $(NAME) references
 * in strings.  Variables defined in Kconfig files take precedence over
 * environment variables; undefined names expand to the empty string.
 * Recursive expansion is capped at depth 32 to detect cycles.
 *
 * Only simple, non-nested $(NAME) is supported.  Nested forms like
 * $(A$(B)) are not expanded — the inner $(...) is treated as literal
 * text within the name lookup, which will fail gracefully (expand to
 * empty or match a literally-named variable).
 */
#include "cconfig/cconfig.h"
#include "ice.h"

/* ------------------------------------------------------------------ */
/*  Character classification helpers                                   */
/* ------------------------------------------------------------------ */

static int is_ident_start(int ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
	       ch == '_';
}

static int is_ident_char(int ch)
{
	return is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

/* ------------------------------------------------------------------ */
/*  Variable lookup                                                    */
/* ------------------------------------------------------------------ */

static const char *var_lookup(const struct kc_symtab *tab, const char *name)
{
	const struct kc_variable *var;

	for (var = tab->variables; var; var = var->next) {
		if (strcmp(var->name, name) == 0)
			return var->value;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/*  Cycle detection during recursive-expand                            */
/* ------------------------------------------------------------------ */

#define KC_EXPAND_DEPTH_MAX 32

static char *expand_with_depth(const struct kc_symtab *tab, const char *raw,
			       int depth)
{
	struct sbuf sb = SBUF_INIT;
	const char *ptr = raw;

	if (depth > KC_EXPAND_DEPTH_MAX)
		die("error: macro expansion depth exceeds %d (cycle?)",
		    KC_EXPAND_DEPTH_MAX);

	while (*ptr) {
		if (ptr[0] == '$' && ptr[1] == '(') {
			const char *name_start = ptr + 2;
			const char *closing = strchr(name_start, ')');

			if (closing) {
				size_t name_len =
				    (size_t)(closing - name_start);
				char *var_name =
				    sbuf_strndup(name_start, name_len);
				const char *val;

				val = var_lookup(tab, var_name);
				if (!val)
					val = getenv(var_name);

				if (val) {
					char *expanded = expand_with_depth(
					    tab, val, depth + 1);
					sbuf_addstr(&sb, expanded);
					free(expanded);
				}

				free(var_name);
				ptr = closing + 1;
				continue;
			}
		}
		/*
		 * ${NAME} forces environment variable lookup only,
		 * bypassing Kconfig variables.  This matches the
		 * esp-idf-kconfig semantics where ${} always reads
		 * from the process environment.
		 */
		if (ptr[0] == '$' && ptr[1] == '{') {
			const char *name_start = ptr + 2;
			const char *closing = strchr(name_start, '}');

			if (closing) {
				size_t name_len =
				    (size_t)(closing - name_start);
				char *var_name =
				    sbuf_strndup(name_start, name_len);
				const char *val;

				val = getenv(var_name);

				if (val) {
					char *expanded = expand_with_depth(
					    tab, val, depth + 1);
					sbuf_addstr(&sb, expanded);
					free(expanded);
				}

				free(var_name);
				ptr = closing + 1;
				continue;
			}
		}
		/* Bare $NAME: same lookup as $(NAME). */
		if (ptr[0] == '$' && is_ident_start(ptr[1])) {
			const char *name_start = ptr + 1;
			const char *name_end = name_start;
			char *var_name;
			const char *val;

			while (is_ident_char(*name_end))
				name_end++;

			var_name = sbuf_strndup(
			    name_start, (size_t)(name_end - name_start));
			val = var_lookup(tab, var_name);
			if (!val)
				val = getenv(var_name);

			if (val) {
				char *expanded =
				    expand_with_depth(tab, val, depth + 1);
				sbuf_addstr(&sb, expanded);
				free(expanded);
			}

			free(var_name);
			ptr = name_end;
			continue;
		}
		sbuf_addch(&sb, *ptr);
		ptr++;
	}

	return sbuf_detach(&sb);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void kc_preproc_set(struct kc_symtab *tab, const char *name, const char *value,
		    int is_immediate)
{
	struct kc_variable *var;

	/* Update existing entry if present. */
	for (var = tab->variables; var; var = var->next) {
		if (strcmp(var->name, name) == 0) {
			char *new_val;

			if (is_immediate)
				new_val = kc_preproc_expand(tab, value);
			else
				new_val = sbuf_strdup(value);
			free(var->value);
			var->value = new_val;
			var->is_immediate = is_immediate;
			return;
		}
	}

	var = xcalloc(1, sizeof(*var));
	var->name = sbuf_strdup(name);
	if (is_immediate) {
		var->value = kc_preproc_expand(tab, value);
	} else {
		var->value = sbuf_strdup(value);
	}
	var->is_immediate = is_immediate;
	var->next = tab->variables;
	tab->variables = var;
}

char *kc_preproc_expand(const struct kc_symtab *tab, const char *raw)
{
	return expand_with_depth(tab, raw, 0);
}

void kc_preproc_release(struct kc_symtab *tab)
{
	struct kc_variable *var = tab->variables;

	while (var) {
		struct kc_variable *next = var->next;

		free(var->name);
		free(var->value);
		free(var);
		var = next;
	}
	tab->variables = NULL;
}
