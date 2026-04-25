/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Recursive-descent evaluator for component-manager @c if: clauses.
 *
 *   expr   := or_expr
 *   or     := and ('||' and)*
 *   and    := atom ('&&' atom)*
 *   atom   := '(' expr ')' | clause
 *   clause := lhs op rhs
 *
 * Tokens are lexed inline (no separate tokenizer) since the grammar
 * is small.  Expressions evaluate as we parse; errors short-circuit
 * via @c st->error.  See rules.h for the public surface.
 */
#include "rules.h"

#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "error.h"
#include "json.h"
#include "sbuf.h"
#include "semver.h"

enum op {
	OP_NONE,
	OP_EQ,
	OP_NE,
	OP_LT,
	OP_LE,
	OP_GT,
	OP_GE,
	OP_IN,
	OP_NIN,
};

struct st {
	const char *p;
	const struct rules_ctx *ctx;
	int error;
};

static void skip_ws(struct st *s)
{
	while (*s->p == ' ' || *s->p == '\t')
		s->p++;
}

/* Word characters as defined by Python's if_parser LEFT_VALUE regex
 * (alphanum + ${}_-.).  Numbers + version literals fall under this
 * since they're alphanumeric runs we hand off to specialised
 * comparison code based on the lhs context. */
static int is_word(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
	       c == '$' || c == '{' || c == '}';
}

static char *read_word(struct st *s)
{
	const char *start;
	skip_ws(s);
	start = s->p;
	while (is_word((unsigned char)*s->p))
		s->p++;
	if (s->p == start)
		return NULL;
	return sbuf_strndup(start, (size_t)(s->p - start));
}

static char *read_qstring(struct st *s)
{
	const char *start;
	char *r;
	if (*s->p != '"')
		return NULL;
	s->p++;
	start = s->p;
	while (*s->p && *s->p != '"')
		s->p++;
	if (*s->p != '"') {
		s->error = 1;
		return NULL;
	}
	r = sbuf_strndup(start, (size_t)(s->p - start));
	s->p++;
	return r;
}

/* Right-hand-side scalar: quoted string OR bare word. */
static char *read_scalar(struct st *s)
{
	skip_ws(s);
	if (*s->p == '"')
		return read_qstring(s);
	return read_word(s);
}

/* Match a literal keyword followed by non-word boundary. */
static int peek_keyword(const struct st *s, const char *kw)
{
	size_t n = strlen(kw);
	if (strncmp(s->p, kw, n) != 0)
		return 0;
	int next = (unsigned char)s->p[n];
	return !is_word(next);
}

static enum op read_op(struct st *s)
{
	skip_ws(s);
	if (s->p[0] == '=' && s->p[1] == '=') {
		s->p += 2;
		return OP_EQ;
	}
	if (s->p[0] == '!' && s->p[1] == '=') {
		s->p += 2;
		return OP_NE;
	}
	if (s->p[0] == '<' && s->p[1] == '=') {
		s->p += 2;
		return OP_LE;
	}
	if (s->p[0] == '>' && s->p[1] == '=') {
		s->p += 2;
		return OP_GE;
	}
	if (s->p[0] == '<') {
		s->p++;
		return OP_LT;
	}
	if (s->p[0] == '>') {
		s->p++;
		return OP_GT;
	}
	if (peek_keyword(s, "in")) {
		s->p += 2;
		return OP_IN;
	}
	if (peek_keyword(s, "not")) {
		const char *save = s->p;
		s->p += 3;
		skip_ws(s);
		if (peek_keyword(s, "in")) {
			s->p += 2;
			return OP_NIN;
		}
		s->p = save;
	}
	return OP_NONE;
}

static const char *op_str(enum op op)
{
	switch (op) {
	case OP_EQ:
		return "==";
	case OP_NE:
		return "!=";
	case OP_LT:
		return "<";
	case OP_LE:
		return "<=";
	case OP_GT:
		return ">";
	case OP_GE:
		return ">=";
	default:
		return NULL;
	}
}

static int apply_string_op(const char *l, enum op op, const char *r)
{
	int cmp = strcmp(l, r);
	switch (op) {
	case OP_EQ:
		return cmp == 0;
	case OP_NE:
		return cmp != 0;
	case OP_LT:
		return cmp < 0;
	case OP_LE:
		return cmp <= 0;
	case OP_GT:
		return cmp > 0;
	case OP_GE:
		return cmp >= 0;
	default:
		return 0;
	}
}

/*
 * idf_version comparisons go through the SemVer machinery: build a
 * constraint string by concatenating op + rhs ("op>=" + "5.0" =
 * ">=5.0"), parse it, and match the version.  Mirrors the Python
 * tool's @c IfClause.eval_spec path.
 */
static int apply_semver_op(const char *idf_str, enum op op, const char *rhs,
			   int *err)
{
	struct semver_version v = SEMVER_VERSION_INIT;
	struct semver_constraint *c;
	struct sbuf spec = SBUF_INIT;
	const char *o;
	int rc = 0;

	if (semver_parse(&v, idf_str) < 0) {
		*err = 1;
		goto out;
	}
	o = op_str(op);
	if (!o) {
		*err = 1;
		goto out;
	}
	sbuf_addstr(&spec, o);
	sbuf_addstr(&spec, rhs);
	c = semver_constraint_parse(spec.buf);
	if (!c) {
		*err = 1;
		goto out;
	}
	rc = semver_constraint_matches(c, &v);
	semver_constraint_free(c);
out:
	semver_release(&v);
	sbuf_release(&spec);
	return rc;
}

/* Look up @p name in sdkconfig.json, formatting the value as a string
 * comparable to what manifest authors write (booleans -> y/n,
 * numbers -> integer string).  Returns a freshly allocated string
 * (caller frees), or NULL if missing. */
static char *sdkconfig_lookup(const struct json_value *cfg, const char *name)
{
	struct json_value *v = json_get(cfg, name);
	if (!v)
		return NULL;
	switch (json_type(v)) {
	case JSON_STRING:
		return sbuf_strdup(json_as_string(v));
	case JSON_BOOL:
		return sbuf_strdup(json_as_bool(v) ? "y" : "n");
	case JSON_NUMBER: {
		char buf[32];
		double d = json_as_number(v);
		long long n = (long long)d;
		if ((double)n == d)
			snprintf(buf, sizeof(buf), "%lld", n);
		else
			snprintf(buf, sizeof(buf), "%g", d);
		return sbuf_strdup(buf);
	}
	default:
		return NULL;
	}
}

static int compare(struct st *s, const char *lhs, enum op op, const char *rhs)
{
	if (!strcmp(lhs, "target")) {
		const char *t = s->ctx->target ? s->ctx->target : "unknown";
		if (op != OP_EQ && op != OP_NE) {
			s->error = 1;
			return 0;
		}
		return apply_string_op(t, op, rhs);
	}
	if (!strcmp(lhs, "idf_version")) {
		const char *iv =
		    s->ctx->idf_version ? s->ctx->idf_version : "0.0.0";
		int err = 0;
		int r = apply_semver_op(iv, op, rhs, &err);
		if (err)
			s->error = 1;
		return r;
	}

	/* Anything else: assume it's a kconfig variable name lookup. */
	{
		char *kval = s->ctx->sdkconfig
				 ? sdkconfig_lookup(s->ctx->sdkconfig, lhs)
				 : NULL;
		int rc;
		if (!kval) {
			s->error = 1;
			return 0;
		}
		rc = apply_string_op(kval, op, rhs);
		free(kval);
		return rc;
	}
}

/*
 * Parse "<lhs> in [a, b, c]" and return whether lhs matches any item.
 * Caller has already consumed the '['.  Membership uses string
 * equality only -- no version-spec semantics inside lists.
 */
static int eval_in_list(struct st *s, const char *lhs)
{
	int found = 0;
	skip_ws(s);
	while (*s->p && *s->p != ']') {
		char *item = read_scalar(s);
		if (!item) {
			s->error = 1;
			return 0;
		}
		if (compare(s, lhs, OP_EQ, item))
			found = 1;
		free(item);
		skip_ws(s);
		if (*s->p == ',') {
			s->p++;
			skip_ws(s);
			continue;
		}
		break;
	}
	if (*s->p != ']') {
		s->error = 1;
		return 0;
	}
	s->p++;
	return found;
}

static int eval_or(struct st *s);

static int eval_clause(struct st *s)
{
	char *lhs;
	enum op op;
	int result = 0;

	skip_ws(s);
	if (*s->p == '(') {
		s->p++;
		result = eval_or(s);
		skip_ws(s);
		if (*s->p != ')') {
			s->error = 1;
			return 0;
		}
		s->p++;
		return result;
	}

	lhs = read_word(s);
	if (!lhs) {
		s->error = 1;
		return 0;
	}

	op = read_op(s);
	if (op == OP_NONE) {
		s->error = 1;
		free(lhs);
		return 0;
	}

	skip_ws(s);
	if (op == OP_IN || op == OP_NIN) {
		int found;
		if (*s->p != '[') {
			s->error = 1;
			free(lhs);
			return 0;
		}
		s->p++;
		found = eval_in_list(s, lhs);
		result = (op == OP_IN) ? found : !found;
	} else {
		char *rhs = read_scalar(s);
		if (!rhs) {
			s->error = 1;
			free(lhs);
			return 0;
		}
		result = compare(s, lhs, op, rhs);
		free(rhs);
	}

	free(lhs);
	return result;
}

static int eval_and(struct st *s)
{
	int v = eval_clause(s);
	for (;;) {
		skip_ws(s);
		if (s->p[0] == '&' && s->p[1] == '&') {
			s->p += 2;
			int r = eval_clause(s);
			v = v && r;
		} else {
			break;
		}
	}
	return v;
}

static int eval_or(struct st *s)
{
	int v = eval_and(s);
	for (;;) {
		skip_ws(s);
		if (s->p[0] == '|' && s->p[1] == '|') {
			s->p += 2;
			int r = eval_and(s);
			v = v || r;
		} else {
			break;
		}
	}
	return v;
}

int rules_eval(const char *expr, const struct rules_ctx *ctx)
{
	struct st s = {.p = expr, .ctx = ctx};
	int r;

	if (!expr || !ctx)
		return -1;

	r = eval_or(&s);
	skip_ws(&s);
	if (s.error || *s.p != '\0')
		return -1;
	return r ? 1 : 0;
}
