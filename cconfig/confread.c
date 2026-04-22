/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file confread.c
 * @brief Read an sdkconfig file and apply values to the symbol table.
 *
 * The sdkconfig format uses CONFIG_FOO=value for set symbols and
 * "# CONFIG_FOO is not set" for explicitly unset bool symbols.
 * Comments (lines starting with #) and blank lines are skipped.
 * String values are stored with surrounding quotes stripped.
 *
 * Call kc_load_config() after parsing the Kconfig tree but before
 * kc_finalize() so loaded values participate in dependency propagation.
 */
#include "cconfig/cconfig.h"
#include "ice.h"

/**
 * Strip trailing whitespace from a NUL-terminated C string using
 * sbuf_rtrim on a temporary wrapper.
 */
static void rtrim_line(char *str)
{
	struct sbuf tmp = {.buf = str, .len = strlen(str), .alloc = 0};

	sbuf_rtrim(&tmp);
}

#define CONFIG_PREFIX "CONFIG_"
#define CONFIG_PREFIX_LEN (sizeof(CONFIG_PREFIX) - 1)

#define UNSET_PREFIX "# CONFIG_"
#define UNSET_PREFIX_LEN (sizeof(UNSET_PREFIX) - 1)

#define UNSET_SUFFIX " is not set"
#define UNSET_SUFFIX_LEN (sizeof(UNSET_SUFFIX) - 1)

/**
 * Check whether a value string is compatible with a symbol's declared
 * type.  Returns 1 if compatible, 0 otherwise.
 */
static int value_matches_type(const char *value, enum kc_sym_type type)
{
	switch (type) {
	case KC_TYPE_BOOL:
		return strcmp(value, "y") == 0 || strcmp(value, "n") == 0;
	case KC_TYPE_INT: {
		char *end;
		int saved_errno = errno;
		errno = 0;
		(void)strtol(value, &end, 10);
		if (errno == ERANGE) {
			errno = saved_errno;
			return 0;
		}
		errno = saved_errno;
		return *end == '\0' && end != value;
	}
	case KC_TYPE_HEX:
		if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
			char *end;
			int saved_errno = errno;
			errno = 0;
			(void)strtoul(value, &end, 16);
			if (errno == ERANGE) {
				errno = saved_errno;
				return 0;
			}
			errno = saved_errno;
			return *end == '\0' && end != value;
		}
		return 0;
	case KC_TYPE_FLOAT: {
		char *end;
		int saved_errno = errno;
		errno = 0;
		(void)strtod(value, &end);
		if (errno == ERANGE) {
			errno = saved_errno;
			return 0;
		}
		errno = saved_errno;
		return *end == '\0' && end != value;
	}
	case KC_TYPE_STRING:
		return 1;
	/*
	 * Unknown-type symbols are already broken; accepting lets the
	 * loader proceed and finalize will sort it out.
	 */
	case KC_TYPE_UNKNOWN:
		return 1;
	}
	return 1;
}

/**
 * Strip surrounding double-quotes from a string value in-place.
 * Returns 1 if an unterminated quote was detected (and warned),
 * 0 otherwise.
 */
static int strip_quotes(char *value, const char *path, int lineno,
			const char *sym_name)
{
	size_t len = strlen(value);

	if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
		memmove(value, value + 1, len - 2);
		value[len - 2] = '\0';
		return 0;
	}
	if (value[0] == '"') {
		warn("%s:%d: unterminated quote for symbol %s", path, lineno,
		     sym_name);
		return 1;
	}
	return 0;
}

/**
 * Process a "# CONFIG_FOO is not set" line.  Sets the matching bool
 * symbol's value to "n".  Returns the number of warnings emitted.
 */
static int handle_unset_line(const char *line, struct kc_symtab *tab,
			     const char *path, int lineno)
{
	const char *name_start;
	const char *suffix;
	size_t name_len;
	char *name;
	struct kc_symbol *sym;

	name_start = line + UNSET_PREFIX_LEN;
	suffix = strstr(name_start, UNSET_SUFFIX);
	if (!suffix || suffix[UNSET_SUFFIX_LEN] != '\0')
		return 0;

	name_len = (size_t)(suffix - name_start);
	if (name_len == 0)
		return 0;

	name = sbuf_strndup(name_start, name_len);
	sym = kc_symtab_lookup(tab, name);

	if (!sym) {
		warn("%s:%d: ignoring unknown symbol %s", path, lineno, name);
		free(name);
		return 1;
	}

	if (sym->type != KC_TYPE_BOOL && sym->type != KC_TYPE_UNKNOWN) {
		warn("%s:%d: 'is not set' applied to non-bool symbol %s "
		     "(type %s)",
		     path, lineno, name, kc_sym_type_name(sym->type));
		free(name);
		return 1;
	}

	free(sym->curr_value);
	sym->curr_value = sbuf_strdup("n");
	sym->flags |= KC_SYM_CHANGED;
	sym->flags &= ~KC_SYM_VALID;
	free(name);
	return 0;
}

/**
 * Process a "CONFIG_FOO=value" assignment line.  Returns the number
 * of warnings emitted.
 *
 * NB: mutates @p line in-place (NUL-terminates at '=' to split
 * name/value).
 */
static int handle_assign_line(char *line, struct kc_symtab *tab,
			      const char *path, int lineno)
{
	char *eq;
	char *sym_name;
	char *value;
	struct kc_symbol *sym;
	int warnings = 0;

	eq = strchr(line + CONFIG_PREFIX_LEN, '=');
	if (!eq) {
		warn("%s:%d: malformed line (missing '='): %s", path, lineno,
		     line);
		return 1;
	}

	*eq = '\0';
	sym_name = line + CONFIG_PREFIX_LEN;
	value = eq + 1;

	if (*sym_name == '\0') {
		warn("%s:%d: malformed line (empty symbol name)", path, lineno);
		return 1;
	}

	sym = kc_symtab_lookup(tab, sym_name);
	if (!sym) {
		warn("%s:%d: ignoring unknown symbol %s", path, lineno,
		     sym_name);
		return 1;
	}

	if (sym->type == KC_TYPE_STRING)
		warnings += strip_quotes(value, path, lineno, sym->name);

	if (!value_matches_type(value, sym->type)) {
		warn("%s:%d: value '%s' does not match type %s for symbol %s",
		     path, lineno, value, kc_sym_type_name(sym->type),
		     sym->name);
		return warnings + 1;
	}

	free(sym->curr_value);
	sym->curr_value = sbuf_strdup(value);
	sym->flags |= KC_SYM_CHANGED;
	sym->flags &= ~KC_SYM_VALID;
	return warnings;
}

/**
 * Load an sdkconfig file and apply values to the symbol table.
 *
 * Returns -1 if the file cannot be read, 0 if no warnings were
 * emitted, or a positive count of warnings on recoverable issues
 * (unknown symbols, type mismatches, malformed lines).
 */
int kc_load_config(const char *path, struct kc_symtab *tab)
{
	struct sbuf file_buf = SBUF_INIT;
	size_t pos = 0;
	char *line;
	int lineno = 0;
	int warnings = 0;

	if (sbuf_read_file(&file_buf, path) < 0)
		return -1;

	while ((line = sbuf_getline(file_buf.buf, file_buf.len, &pos))) {
		lineno++;
		rtrim_line(line);

		if (*line == '\0')
			continue;

		if (line[0] == '#') {
			if (strncmp(line, UNSET_PREFIX, UNSET_PREFIX_LEN) == 0)
				warnings +=
				    handle_unset_line(line, tab, path, lineno);
			continue;
		}

		/* handle_assign_line mutates line in-place (splits at '=') */
		if (strncmp(line, CONFIG_PREFIX, CONFIG_PREFIX_LEN) == 0)
			warnings += handle_assign_line(line, tab, path, lineno);
	}

	sbuf_release(&file_buf);
	return warnings;
}
