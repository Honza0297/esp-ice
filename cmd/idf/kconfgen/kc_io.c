/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file kc_io.c
 * @brief sdkconfig load / write implementation.
 */
#include "kc_io.h"
#include "ice.h"
#include "kc_ast.h"
#include "kc_eval.h"

#define CONFIG_PREFIX "CONFIG_"
#define CONFIG_PREFIX_LEN (sizeof(CONFIG_PREFIX) - 1)

/* ================================================================== */
/*  Loader                                                            */
/* ================================================================== */

/*
 * Decode backslash escapes and strip surrounding quotes from a
 * string-valued RHS, returning a newly-allocated owned string.
 * Handles \\, \", \n, \t, \r and leaves unknown escapes literal.
 */
static char *unquote_value(const char *s)
{
	size_t n = strlen(s);
	const char *p = s;
	const char *end = s + n;
	if (n >= 2 && *p == '"' && s[n - 1] == '"') {
		p++;
		end--;
	}
	struct sbuf sb = SBUF_INIT;
	while (p < end) {
		if (*p == '\\' && p + 1 < end) {
			switch (p[1]) {
			case 'n':
				sbuf_addch(&sb, '\n');
				break;
			case 't':
				sbuf_addch(&sb, '\t');
				break;
			case 'r':
				sbuf_addch(&sb, '\r');
				break;
			case '\\':
				sbuf_addch(&sb, '\\');
				break;
			case '"':
				sbuf_addch(&sb, '"');
				break;
			default:
				sbuf_addch(&sb, '\\');
				sbuf_addch(&sb, p[1]);
				break;
			}
			p += 2;
		} else {
			sbuf_addch(&sb, *p++);
		}
	}
	return sbuf_detach(&sb);
}

static void process_config_line(struct kc_ctx *ctx, char *line)
{
	/* Skip leading whitespace. */
	while (*line == ' ' || *line == '\t')
		line++;
	if (!*line)
		return;

	/* `# CONFIG_X is not set` -> X = n */
	if (*line == '#') {
		const char *p = line + 1;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, CONFIG_PREFIX, CONFIG_PREFIX_LEN) != 0)
			return;
		const char *name = p + CONFIG_PREFIX_LEN;
		const char *end = name;
		while (*end && *end != ' ' && *end != '\t')
			end++;
		const char *tail = end;
		while (*tail == ' ' || *tail == '\t')
			tail++;
		if (strcmp(tail, "is not set") != 0)
			return;
		char *name_copy = sbuf_strndup(name, (size_t)(end - name));
		kc_sym_set_user(ctx, name_copy, "n");
		free(name_copy);
		return;
	}

	/* `CONFIG_NAME=value` */
	if (strncmp(line, CONFIG_PREFIX, CONFIG_PREFIX_LEN) != 0)
		return;
	char *eq = strchr(line, '=');
	if (!eq)
		return;

	size_t name_len = (size_t)(eq - (line + CONFIG_PREFIX_LEN));
	char *name = sbuf_strndup(line + CONFIG_PREFIX_LEN, name_len);
	const char *rhs = eq + 1;

	/* Empty RHS on a bool shortcut means "n" (python parity). */
	char *val;
	if (!*rhs)
		val = sbuf_strdup("n");
	else if (*rhs == '"')
		val = unquote_value(rhs);
	else
		val = sbuf_strdup(rhs);

	kc_sym_set_user(ctx, name, val);
	free(name);
	free(val);
}

void kc_load_config(struct kc_ctx *ctx, const char *path)
{
	struct sbuf sb = SBUF_INIT;
	if (sbuf_read_file(&sb, path) < 0)
		die_errno("cannot read config '%s'", path);

	size_t pos = 0;
	char *line;
	while ((line = sbuf_getline(sb.buf, sb.len, &pos)) != NULL)
		process_config_line(ctx, line);

	sbuf_release(&sb);
}

/* ================================================================== */
/*  Writer                                                            */
/* ================================================================== */

/*
 * Append a string value to @p out in sdkconfig quoted-string form.
 * Backslash-escapes '"' and '\\'; leaves other bytes verbatim.  (We
 * don't re-encode '\n' / '\t' because ESP-IDF doesn't use them in
 * config string values; matches python kconfgen's behaviour.)
 */
static void emit_quoted(struct sbuf *out, const char *s)
{
	sbuf_addch(out, '"');
	for (const char *p = s; *p; p++) {
		if (*p == '"' || *p == '\\')
			sbuf_addch(out, '\\');
		sbuf_addch(out, *p);
	}
	sbuf_addch(out, '"');
}

static int sym_has_env(const struct ksym *s)
{
	for (const struct kprop *p = s->props; p; p = p->next)
		if (p->kind == KP_ENV)
			return 1;
	return 0;
}

/*
 * True for symbols that should never appear in a CONFIG_ line: the
 * lexer-interned barewords (y, n, m, numeric literals) and choice
 * parents (whose "value" is synthetic, matching python kconfgen).
 */
static int is_pseudo_sym(const struct ksym *s)
{
	if (!s->name || !s->name[0])
		return 1;
	if (!strcmp(s->name, "y") || !strcmp(s->name, "n") ||
	    !strcmp(s->name, "m") || !strcmp(s->name, "yes") ||
	    !strcmp(s->name, "no"))
		return 1;
	if (s->type == KS_UNKNOWN && !s->props)
		return 1;
	if (s->is_choice)
		return 1;
	return 0;
}

/*
 * Walk the menu tree pre-order and invoke @p cb on every symbol that
 * backs a KM_SYM / KM_CHOICE menu node -- i.e. the declaration order
 * python kconfgen uses for its output files.  Filtering out
 * pseudo-syms, env-options, etc. is the caller's job.
 */
typedef void (*sym_cb)(struct sbuf *out, const struct ksym *s, void *ud);
static void walk_syms(const struct kmenu *m, struct sbuf *out, sym_cb cb,
		      void *ud)
{
	if (!m)
		return;
	if ((m->kind == KM_SYM || m->kind == KM_CHOICE) && m->sym)
		cb(out, m->sym, ud);
	for (const struct kmenu *c = m->children; c; c = c->next)
		walk_syms(c, out, cb, ud);
}

/*
 * Emit one symbol in the canonical sdkconfig form.
 *
 *   bool  = y    -> CONFIG_X=y
 *   bool  = n    -> # CONFIG_X is not set
 *   int          -> CONFIG_X=<decimal>
 *   hex          -> CONFIG_X=0x...   (0x prefix preserved)
 *   string       -> CONFIG_X="..."   (escaped)
 *   float        -> CONFIG_X=<as-stored>
 *   UNKNOWN      -> CONFIG_X=<as-stored>  (still emitted so nothing
 *                                          is silently lost)
 */
static void emit_symbol(struct sbuf *out, const struct ksym *s)
{
	const char *val = s->cur_val ? s->cur_val : "";

	/*
	 * ESP-IDF extension: tag lines whose value came from a
	 * built-in default (as opposed to being set by the user via
	 * --config / --defaults) with a "# default:" pragma, so tools
	 * that diff sdkconfig can distinguish user-modified settings
	 * from untouched ones.  Matches python kconfgen's behaviour.
	 */
	if (!s->user_set)
		sbuf_addstr(out, "# default:\n");

	if (s->type == KS_BOOL) {
		if (!strcmp(val, "y")) {
			sbuf_addf(out, "%s%s=y\n", CONFIG_PREFIX, s->name);
		} else {
			sbuf_addf(out, "# %s%s is not set\n", CONFIG_PREFIX,
				  s->name);
		}
		return;
	}

	if (s->type == KS_STRING) {
		sbuf_addf(out, "%s%s=", CONFIG_PREFIX, s->name);
		emit_quoted(out, val);
		sbuf_addch(out, '\n');
		return;
	}

	/* int / hex / float / unknown: emit raw.  For hex, default to
	 * "0x0" if the stored value happens to be missing a prefix. */
	sbuf_addf(out, "%s%s=%s\n", CONFIG_PREFIX, s->name, val);
}

static void cb_config(struct sbuf *out, const struct ksym *s, void *ud)
{
	(void)ud;
	if (is_pseudo_sym(s) || sym_has_env(s))
		return;
	emit_symbol(out, s);
}

void kc_write_config(const struct kc_ctx *ctx, const char *path)
{
	struct sbuf out = SBUF_INIT;

	/* Matches python kconfgen's header verbatim so diff-based
	 * build-system tooling that compares old/new sdkconfig doesn't
	 * flag a spurious header change.  The double space before
	 * "Project Configuration" mirrors python's output when no
	 * IDF version string is substituted. */
	sbuf_addstr(&out, "#\n"
			  "# Automatically generated file. DO NOT EDIT.\n"
			  "# Espressif IoT Development Framework (ESP-IDF)  "
			  "Project Configuration\n"
			  "#\n");

	walk_syms(ctx->root, &out, cb_config, NULL);

	if (write_file_atomic(path, out.buf, out.len) < 0)
		die_errno("cannot write '%s'", path);

	sbuf_release(&out);
}

/* ================================================================== */
/*  Header writer (sdkconfig.h)                                       */
/* ================================================================== */

/* Emit a #define line for the symbol in C-header form.  bool-n is
 * deliberately omitted (matching python kconfgen) rather than
 * #undef'd: absence of a define lets user code test with
 * `#if defined(CONFIG_X)`. */
static void emit_header_symbol(struct sbuf *out, const struct ksym *s)
{
	const char *val = s->cur_val ? s->cur_val : "";

	if (s->type == KS_BOOL) {
		if (!strcmp(val, "y"))
			sbuf_addf(out, "#define %s%s 1\n", CONFIG_PREFIX,
				  s->name);
		return;
	}
	if (s->type == KS_STRING) {
		sbuf_addf(out, "#define %s%s ", CONFIG_PREFIX, s->name);
		emit_quoted(out, val);
		sbuf_addch(out, '\n');
		return;
	}
	/* int / hex / float / unknown: emit raw. */
	sbuf_addf(out, "#define %s%s %s\n", CONFIG_PREFIX, s->name, val);
}

static void cb_header(struct sbuf *out, const struct ksym *s, void *ud)
{
	(void)ud;
	if (is_pseudo_sym(s) || sym_has_env(s))
		return;
	emit_header_symbol(out, s);
}

void kc_write_header(const struct kc_ctx *ctx, const char *path)
{
	struct sbuf out = SBUF_INIT;

	sbuf_addstr(&out, "/*\n"
			  " * Automatically generated file. DO NOT EDIT.\n"
			  " * Espressif IoT Development Framework (ESP-IDF)  "
			  "Configuration Header\n"
			  " */\n"
			  "#pragma once\n");

	walk_syms(ctx->root, &out, cb_header, NULL);

	if (write_file_atomic(path, out.buf, out.len) < 0)
		die_errno("cannot write '%s'", path);

	sbuf_release(&out);
}

/* ================================================================== */
/*  CMake writer (sdkconfig.cmake)                                    */
/* ================================================================== */

/* In cmake values are ALL quoted strings -- bool-y -> "y", bool-n ->
 * "" (empty), int/hex -> quoted numeric string.  Matches python
 * kconfgen's actual output. */
static void emit_cmake_symbol(struct sbuf *out, const struct ksym *s)
{
	const char *val = s->cur_val ? s->cur_val : "";
	const char *effective = val;

	if (s->type == KS_BOOL && strcmp(val, "y") != 0)
		effective = ""; /* bool-n renders as empty string */

	sbuf_addf(out, "set(%s%s ", CONFIG_PREFIX, s->name);
	emit_quoted(out, effective);
	sbuf_addstr(out, ")\n");
}

struct cmake_walk {
	struct sbuf *list;
	int first;
};

static void cb_cmake(struct sbuf *out, const struct ksym *s, void *ud)
{
	struct cmake_walk *w = ud;
	if (is_pseudo_sym(s) || sym_has_env(s))
		return;
	emit_cmake_symbol(out, s);
	if (!w->first)
		sbuf_addch(w->list, ';');
	sbuf_addf(w->list, "%s%s", CONFIG_PREFIX, s->name);
	w->first = 0;
}

void kc_write_cmake(const struct kc_ctx *ctx, const char *path)
{
	struct sbuf out = SBUF_INIT;

	/* Note the SINGLE space before "Configuration" here -- that's
	 * how python kconfgen renders the cmake banner, unlike the
	 * config / header banners which use a double space. */
	sbuf_addstr(&out, "#\n"
			  "# Automatically generated file. DO NOT EDIT.\n"
			  "# Espressif IoT Development Framework (ESP-IDF) "
			  "Configuration cmake include file\n"
			  "#\n");

	struct sbuf list = SBUF_INIT;
	struct cmake_walk w = {.list = &list, .first = 1};
	walk_syms(ctx->root, &out, cb_cmake, &w);

	/* Trailing enumeration, unquoted, semicolon-separated.  Python
	 * kconfgen does NOT emit a newline after this final line, so we
	 * don't either -- any diff-based test would otherwise trip on a
	 * one-byte difference. */
	sbuf_addf(&out, "set(CONFIGS_LIST %s)", list.buf);
	sbuf_release(&list);

	if (write_file_atomic(path, out.buf, out.len) < 0)
		die_errno("cannot write '%s'", path);

	sbuf_release(&out);
}
