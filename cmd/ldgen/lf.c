/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lf.c
 * @brief Linker fragment (.lf) parser -- hand-written LL(1).
 *
 * Two-layer design:
 *   1. Lexer  -- character stream to token stream, with INDENT/DEDENT
 *   2. Parser -- recursive descent consuming the token stream
 */
#include "../../ice.h"
#include "lf.h"

/* ================================================================== */
/*  Helpers                                                           */
/* ================================================================== */

#define VEC_PUSH(v, n, cap)                                            \
	do {                                                           \
		if ((n) >= (cap)) {                                    \
			(cap) = (cap) ? (cap) * 2 : 4;                \
			(v) = realloc((v), (cap) * sizeof(*(v)));      \
			if (!(v))                                      \
				die_errno("realloc");                  \
		}                                                      \
	} while (0)

static int is_name_start(int c)
{
	return isalpha(c) || c == '_' || c == '.';
}

static int is_name_cont(int c)
{
	return isalnum(c) || c == '_' || c == '.' || c == '$' || c == '+';
}

/* ================================================================== */
/*  Lexer                                                             */
/* ================================================================== */

enum lf_tok {
	TOK_EOF = 0,
	TOK_NL,
	TOK_INDENT,
	TOK_DEDENT,
	TOK_LBRACK,
	TOK_RBRACK,
	TOK_LPAREN,
	TOK_RPAREN,
	TOK_COLON,
	TOK_SEMI,
	TOK_COMMA,
	TOK_ARROW,
	TOK_STAR,
	TOK_NAME,
	TOK_NUM,
};

struct lexer {
	const char *pos;
	const char *path;
	int line;

	int indent[64];
	int depth;

	int pending[64];
	int qh, qt;

	int tok;
	char *val;
	int num;

	int bol;
	int eof;
};

static const char *tok_str(int t)
{
	switch (t) {
	case TOK_EOF:    return "EOF";
	case TOK_NL:     return "newline";
	case TOK_INDENT: return "INDENT";
	case TOK_DEDENT: return "DEDENT";
	case TOK_LBRACK: return "'['";
	case TOK_RBRACK: return "']'";
	case TOK_LPAREN: return "'('";
	case TOK_RPAREN: return "')'";
	case TOK_COLON:  return "':'";
	case TOK_SEMI:   return "';'";
	case TOK_COMMA:  return "','";
	case TOK_ARROW:  return "'->'";
	case TOK_STAR:   return "'*'";
	case TOK_NAME:   return "name";
	case TOK_NUM:    return "number";
	default:         return "?";
	}
}

static void q_push(struct lexer *l, int t) { l->pending[l->qt++ & 63] = t; }
static int  q_pop(struct lexer *l)         { return l->pending[l->qh++ & 63]; }
static int  q_any(struct lexer *l)         { return l->qh < l->qt; }

static void process_indent(struct lexer *l, int indent)
{
	int top = l->indent[l->depth];

	if (indent > top) {
		if (l->depth + 1 >= 64)
			die("%s:%d: indentation too deep", l->path, l->line);
		l->indent[++l->depth] = indent;
		q_push(l, TOK_INDENT);
	} else if (indent < top) {
		while (l->depth > 0 && l->indent[l->depth] > indent) {
			l->depth--;
			q_push(l, TOK_DEDENT);
		}
		if (l->indent[l->depth] != indent)
			die("%s:%d: indentation mismatch", l->path, l->line);
	}
}

static int lf_next(struct lexer *l)
{
	if (q_any(l))
		return l->tok = q_pop(l);
	if (l->eof)
		return l->tok = TOK_EOF;

	/* BOL: skip blank/comment lines, emit INDENT/DEDENT */
	if (l->bol) {
		l->bol = 0;
		for (;;) {
			int indent = 0;
			while (*l->pos == ' ' || *l->pos == '\t') {
				indent++;
				l->pos++;
			}
			if (*l->pos == '\n') {
				l->pos++;
				l->line++;
				continue;
			}
			if (*l->pos == '#') {
				while (*l->pos && *l->pos != '\n')
					l->pos++;
				if (*l->pos == '\n') {
					l->pos++;
					l->line++;
				}
				continue;
			}
			if (*l->pos == '\0') {
				while (l->depth > 0) {
					q_push(l, TOK_DEDENT);
					l->depth--;
				}
				l->eof = 1;
				return l->tok = q_any(l)
					? q_pop(l) : TOK_EOF;
			}
			process_indent(l, indent);
			if (q_any(l))
				return l->tok = q_pop(l);
			break;
		}
	}

	while (*l->pos == ' ' || *l->pos == '\t')
		l->pos++;

	if (*l->pos == '\n') {
		l->pos++;
		l->line++;
		l->bol = 1;
		return l->tok = TOK_NL;
	}

	if (*l->pos == '#') {
		while (*l->pos && *l->pos != '\n')
			l->pos++;
		if (*l->pos == '\n') {
			l->pos++;
			l->line++;
		}
		l->bol = 1;
		return l->tok = TOK_NL;
	}

	if (*l->pos == '\0') {
		q_push(l, TOK_NL);
		while (l->depth > 0) {
			q_push(l, TOK_DEDENT);
			l->depth--;
		}
		l->eof = 1;
		return l->tok = q_pop(l);
	}

	if (l->pos[0] == '-' && l->pos[1] == '>') {
		l->pos += 2;
		return l->tok = TOK_ARROW;
	}

	switch (*l->pos) {
	case '[': l->pos++; return l->tok = TOK_LBRACK;
	case ']': l->pos++; return l->tok = TOK_RBRACK;
	case '(': l->pos++; return l->tok = TOK_LPAREN;
	case ')': l->pos++; return l->tok = TOK_RPAREN;
	case ':': l->pos++; return l->tok = TOK_COLON;
	case ';': l->pos++; return l->tok = TOK_SEMI;
	case ',': l->pos++; return l->tok = TOK_COMMA;
	case '*': l->pos++; return l->tok = TOK_STAR;
	}

	if (is_name_start(*l->pos)) {
		const char *start = l->pos++;
		while (is_name_cont(*l->pos))
			l->pos++;
		while (*l->pos == '-' && l->pos[1] != '>'
		       && is_name_cont(l->pos[1])) {
			l->pos++;
			while (is_name_cont(*l->pos))
				l->pos++;
		}
		free(l->val);
		l->val = sbuf_strndup(start, (size_t)(l->pos - start));
		return l->tok = TOK_NAME;
	}

	if (isdigit((unsigned char)*l->pos)) {
		l->num = 0;
		while (isdigit((unsigned char)*l->pos))
			l->num = l->num * 10 + (*l->pos++ - '0');
		return l->tok = TOK_NUM;
	}

	die("%s:%d: unexpected character '%c' (0x%02x)",
	    l->path, l->line, *l->pos, (unsigned char)*l->pos);
	return TOK_EOF;
}

/* ================================================================== */
/*  Parser helpers                                                    */
/* ================================================================== */

static int is_kw(struct lexer *l, const char *kw)
{
	return l->tok == TOK_NAME && !strcmp(l->val, kw);
}

static void expect(struct lexer *l, int tok)
{
	if (l->tok != tok)
		die("%s:%d: expected %s, got %s",
		    l->path, l->line, tok_str(tok), tok_str(l->tok));
	lf_next(l);
}

static char *expect_name(struct lexer *l)
{
	if (l->tok != TOK_NAME)
		die("%s:%d: expected name, got %s",
		    l->path, l->line, tok_str(l->tok));
	char *s = l->val;
	l->val = NULL;
	lf_next(l);
	return s;
}

/**
 * Read condition expression after "if" / "elif".
 * l->pos is right after the keyword.  Reads up to ':', trims, advances.
 */
static char *read_cond(struct lexer *l)
{
	while (*l->pos == ' ' || *l->pos == '\t')
		l->pos++;

	const char *start = l->pos;
	while (*l->pos && *l->pos != ':' && *l->pos != '\n')
		l->pos++;

	if (*l->pos != ':')
		die("%s:%d: expected ':' after condition", l->path, l->line);

	const char *end = l->pos;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
		end--;

	l->pos++;
	char *expr = sbuf_strndup(start, (size_t)(end - start));
	lf_next(l);
	return expr;
}

/* ================================================================== */
/*  Parser -- entry-level conditionals                                */
/* ================================================================== */

typedef void (*stmt_parser_fn)(struct lexer *l,
			       struct lf_stmt **v, int *n, int *cap);

static void parse_cond(struct lexer *l,
		       struct lf_stmt **v, int *n, int *cap,
		       stmt_parser_fn inner)
{
	VEC_PUSH(*v, *n, *cap);
	struct lf_stmt *s = &(*v)[(*n)++];
	memset(s, 0, sizeof(*s));
	s->is_cond = 1;

	int bcap = 0;
	struct lf_branch *branches = NULL;
	int nb = 0;

	/* if */
	char *expr = read_cond(l);
	expect(l, TOK_NL);

	VEC_PUSH(branches, nb, bcap);
	struct lf_branch *b = &branches[nb++];
	memset(b, 0, sizeof(*b));
	b->expr = expr;

	int scap = 0;
	expect(l, TOK_INDENT);
	while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
		inner(l, &b->stmts, &b->n_stmts, &scap);
	expect(l, TOK_DEDENT);

	/* elif */
	while (is_kw(l, "elif")) {
		expr = read_cond(l);
		expect(l, TOK_NL);

		VEC_PUSH(branches, nb, bcap);
		b = &branches[nb++];
		memset(b, 0, sizeof(*b));
		b->expr = expr;

		scap = 0;
		expect(l, TOK_INDENT);
		while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
			inner(l, &b->stmts, &b->n_stmts, &scap);
		expect(l, TOK_DEDENT);
	}

	/* else */
	if (is_kw(l, "else")) {
		lf_next(l);
		expect(l, TOK_COLON);
		expect(l, TOK_NL);

		VEC_PUSH(branches, nb, bcap);
		b = &branches[nb++];
		memset(b, 0, sizeof(*b));

		scap = 0;
		expect(l, TOK_INDENT);
		while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
			inner(l, &b->stmts, &b->n_stmts, &scap);
		expect(l, TOK_DEDENT);
	}

	s->u.cond.branches = branches;
	s->u.cond.n_branches = nb;
}

/* ================================================================== */
/*  Parser -- entry types                                             */
/* ================================================================== */

static void parse_sec_stmts(struct lexer *l,
			     struct lf_stmt **v, int *n, int *cap)
{
	while (l->tok == TOK_NAME) {
		if (is_kw(l, "if")) {
			parse_cond(l, v, n, cap, parse_sec_stmts);
			continue;
		}
		if (is_kw(l, "elif") || is_kw(l, "else"))
			break;

		VEC_PUSH(*v, *n, *cap);
		struct lf_stmt *s = &(*v)[(*n)++];
		memset(s, 0, sizeof(*s));
		s->u.entry.name = expect_name(l);
		expect(l, TOK_NL);
	}
}

static void parse_sch_stmts(struct lexer *l,
			     struct lf_stmt **v, int *n, int *cap)
{
	while (l->tok == TOK_NAME) {
		if (is_kw(l, "if")) {
			parse_cond(l, v, n, cap, parse_sch_stmts);
			continue;
		}
		if (is_kw(l, "elif") || is_kw(l, "else"))
			break;

		VEC_PUSH(*v, *n, *cap);
		struct lf_stmt *s = &(*v)[(*n)++];
		memset(s, 0, sizeof(*s));
		s->u.entry.name = expect_name(l);
		expect(l, TOK_ARROW);
		s->u.entry.target = expect_name(l);
		expect(l, TOK_NL);
	}
}

static void skip_flag_block(struct lexer *l)
{
	/* TODO: parse flags into AST */
	lf_next(l);
	expect(l, TOK_NL);
	if (l->tok == TOK_INDENT) {
		lf_next(l);
		while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
			lf_next(l);
		expect(l, TOK_DEDENT);
	}
}

static void parse_map_stmts(struct lexer *l,
			     struct lf_stmt **v, int *n, int *cap)
{
	while (l->tok == TOK_NAME || l->tok == TOK_STAR) {
		if (is_kw(l, "if")) {
			parse_cond(l, v, n, cap, parse_map_stmts);
			continue;
		}
		if (is_kw(l, "elif") || is_kw(l, "else"))
			break;

		char *name;
		char *symbol = NULL;

		if (l->tok == TOK_STAR) {
			name = sbuf_strdup("*");
			lf_next(l);
		} else {
			name = expect_name(l);
			if (l->tok == TOK_COLON) {
				lf_next(l);
				symbol = expect_name(l);
			}
		}
		expect(l, TOK_LPAREN);
		char *scheme = expect_name(l);
		expect(l, TOK_RPAREN);

		VEC_PUSH(*v, *n, *cap);
		struct lf_stmt *s = &(*v)[(*n)++];
		memset(s, 0, sizeof(*s));
		s->u.entry.name = name;
		s->u.entry.target = symbol;
		s->u.entry.scheme = scheme;

		if (l->tok == TOK_SEMI)
			skip_flag_block(l);
		else
			expect(l, TOK_NL);
	}
}

/* ================================================================== */
/*  Parser -- fragments                                               */
/* ================================================================== */

static void parse_frags(struct lexer *l,
			struct lf_frag **v, int *n, int *cap);

static void parse_frag_cond(struct lexer *l,
			    struct lf_frag **v, int *n, int *cap)
{
	VEC_PUSH(*v, *n, *cap);
	struct lf_frag *f = &(*v)[(*n)++];
	memset(f, 0, sizeof(*f));
	f->kind = LF_FRAG_COND;

	int bcap = 0;
	struct lf_frag_branch *branches = NULL;
	int nb = 0;

	/* if */
	char *expr = read_cond(l);
	expect(l, TOK_NL);

	VEC_PUSH(branches, nb, bcap);
	struct lf_frag_branch *b = &branches[nb++];
	memset(b, 0, sizeof(*b));
	b->expr = expr;

	int fcap = 0;
	expect(l, TOK_INDENT);
	while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
		parse_frags(l, &b->frags, &b->n_frags, &fcap);
	expect(l, TOK_DEDENT);

	/* elif */
	while (is_kw(l, "elif")) {
		expr = read_cond(l);
		expect(l, TOK_NL);
		VEC_PUSH(branches, nb, bcap);
		b = &branches[nb++];
		memset(b, 0, sizeof(*b));
		b->expr = expr;
		fcap = 0;
		expect(l, TOK_INDENT);
		while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
			parse_frags(l, &b->frags, &b->n_frags, &fcap);
		expect(l, TOK_DEDENT);
	}

	/* else */
	if (is_kw(l, "else")) {
		lf_next(l);
		expect(l, TOK_COLON);
		expect(l, TOK_NL);
		VEC_PUSH(branches, nb, bcap);
		b = &branches[nb++];
		memset(b, 0, sizeof(*b));
		fcap = 0;
		expect(l, TOK_INDENT);
		while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
			parse_frags(l, &b->frags, &b->n_frags, &fcap);
		expect(l, TOK_DEDENT);
	}

	f->u.cond.branches = branches;
	f->u.cond.n = nb;
}

static void parse_entries(struct lexer *l, stmt_parser_fn fn,
			  struct lf_stmt **v, int *n, int *cap)
{
	if (!is_kw(l, "entries"))
		die("%s:%d: expected 'entries'", l->path, l->line);
	lf_next(l);
	expect(l, TOK_COLON);
	expect(l, TOK_NL);

	if (l->tok == TOK_INDENT) {
		lf_next(l);
		while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
			fn(l, v, n, cap);
		expect(l, TOK_DEDENT);
	} else {
		fn(l, v, n, cap);
	}
}

static void parse_sections(struct lexer *l,
			   struct lf_frag **v, int *n, int *cap)
{
	lf_next(l);
	expect(l, TOK_COLON);
	char *name = expect_name(l);
	expect(l, TOK_RBRACK);
	expect(l, TOK_NL);

	struct lf_stmt *stmts = NULL;
	int ns = 0, scap = 0;
	parse_entries(l, parse_sec_stmts, &stmts, &ns, &scap);

	VEC_PUSH(*v, *n, *cap);
	struct lf_frag *f = &(*v)[(*n)++];
	memset(f, 0, sizeof(*f));
	f->kind = LF_SECTIONS;
	f->u.sec.name = name;
	f->u.sec.stmts = stmts;
	f->u.sec.n = ns;
}

static void parse_scheme(struct lexer *l,
			 struct lf_frag **v, int *n, int *cap)
{
	lf_next(l);
	expect(l, TOK_COLON);
	char *name = expect_name(l);
	expect(l, TOK_RBRACK);
	expect(l, TOK_NL);

	struct lf_stmt *stmts = NULL;
	int ns = 0, scap = 0;
	parse_entries(l, parse_sch_stmts, &stmts, &ns, &scap);

	VEC_PUSH(*v, *n, *cap);
	struct lf_frag *f = &(*v)[(*n)++];
	memset(f, 0, sizeof(*f));
	f->kind = LF_SCHEME;
	f->u.sch.name = name;
	f->u.sch.stmts = stmts;
	f->u.sch.n = ns;
}

static void parse_archive_stmts(struct lexer *l,
				struct lf_stmt **v, int *n, int *cap)
{
	while (l->tok == TOK_NAME || l->tok == TOK_STAR) {
		if (is_kw(l, "if")) {
			parse_cond(l, v, n, cap, parse_archive_stmts);
			continue;
		}
		if (is_kw(l, "elif") || is_kw(l, "else"))
			break;

		VEC_PUSH(*v, *n, *cap);
		struct lf_stmt *s = &(*v)[(*n)++];
		memset(s, 0, sizeof(*s));
		if (l->tok == TOK_STAR) {
			s->u.entry.name = sbuf_strdup("*");
			lf_next(l);
		} else {
			s->u.entry.name = expect_name(l);
		}
		expect(l, TOK_NL);
	}
}

static void parse_mapping(struct lexer *l,
			  struct lf_frag **v, int *n, int *cap)
{
	lf_next(l);
	expect(l, TOK_COLON);
	char *name = expect_name(l);
	expect(l, TOK_RBRACK);
	expect(l, TOK_NL);

	/* archive: VALUE NL  or  archive: NL INDENT stmts DEDENT */
	if (!is_kw(l, "archive"))
		die("%s:%d: expected 'archive'", l->path, l->line);
	lf_next(l);
	expect(l, TOK_COLON);

	struct lf_stmt *archive = NULL;
	int na = 0, acap = 0;

	if (l->tok == TOK_NAME || l->tok == TOK_STAR) {
		/* inline value */
		VEC_PUSH(archive, na, acap);
		struct lf_stmt *s = &archive[na++];
		memset(s, 0, sizeof(*s));
		if (l->tok == TOK_STAR) {
			s->u.entry.name = sbuf_strdup("*");
			lf_next(l);
		} else {
			s->u.entry.name = expect_name(l);
		}
		expect(l, TOK_NL);
	} else {
		/* conditional block */
		expect(l, TOK_NL);
		expect(l, TOK_INDENT);
		while (l->tok != TOK_DEDENT && l->tok != TOK_EOF)
			parse_archive_stmts(l, &archive, &na, &acap);
		expect(l, TOK_DEDENT);
	}

	/* entries */
	struct lf_stmt *entries = NULL;
	int ne = 0, ecap = 0;
	parse_entries(l, parse_map_stmts, &entries, &ne, &ecap);

	VEC_PUSH(*v, *n, *cap);
	struct lf_frag *f = &(*v)[(*n)++];
	memset(f, 0, sizeof(*f));
	f->kind = LF_MAPPING;
	f->u.map.name = name;
	f->u.map.archive = archive;
	f->u.map.n_archive = na;
	f->u.map.entries = entries;
	f->u.map.n_entries = ne;
}

static void parse_frags(struct lexer *l,
			struct lf_frag **v, int *n, int *cap)
{
	while (l->tok != TOK_EOF && l->tok != TOK_DEDENT) {
		if (l->tok == TOK_NL) {
			lf_next(l);
			continue;
		}
		if (is_kw(l, "if")) {
			parse_frag_cond(l, v, n, cap);
			continue;
		}
		if (l->tok != TOK_LBRACK)
			die("%s:%d: expected '[' or 'if', got %s",
			    l->path, l->line, tok_str(l->tok));
		lf_next(l);

		if (is_kw(l, "sections"))
			parse_sections(l, v, n, cap);
		else if (is_kw(l, "scheme"))
			parse_scheme(l, v, n, cap);
		else if (is_kw(l, "mapping"))
			parse_mapping(l, v, n, cap);
		else
			die("%s:%d: unknown fragment type '%s'",
			    l->path, l->line,
			    l->tok == TOK_NAME ? l->val : tok_str(l->tok));
	}
}

/* ================================================================== */
/*  Public API                                                        */
/* ================================================================== */

struct lf_file *lf_parse(const char *src, const char *path)
{
	struct lexer l;
	memset(&l, 0, sizeof(l));
	l.pos = src;
	l.path = path;
	l.line = 1;
	l.bol = 1;

	lf_next(&l);

	struct lf_file *f = calloc(1, sizeof(*f));
	if (!f)
		die_errno("calloc");
	f->path = sbuf_strdup(path);

	int cap = 0;
	parse_frags(&l, &f->frags, &f->n_frags, &cap);

	if (l.tok != TOK_EOF)
		die("%s:%d: trailing content after fragments",
		    l.path, l.line);

	free(l.val);
	return f;
}

/* ================================================================== */
/*  Free                                                              */
/* ================================================================== */

static void free_entry(struct lf_entry *e)
{
	free(e->name);
	free(e->target);
	free(e->scheme);
}

static void free_stmts(struct lf_stmt *v, int n);

static void free_branch(struct lf_branch *b)
{
	free(b->expr);
	free_stmts(b->stmts, b->n_stmts);
}

static void free_stmts(struct lf_stmt *v, int n)
{
	for (int i = 0; i < n; i++) {
		if (v[i].is_cond) {
			for (int j = 0; j < v[i].u.cond.n_branches; j++)
				free_branch(&v[i].u.cond.branches[j]);
			free(v[i].u.cond.branches);
		} else {
			free_entry(&v[i].u.entry);
		}
	}
	free(v);
}

static void free_frag_branch(struct lf_frag_branch *v, int n);

static void free_frag(struct lf_frag *f)
{
	switch (f->kind) {
	case LF_SECTIONS:
		free(f->u.sec.name);
		free_stmts(f->u.sec.stmts, f->u.sec.n);
		break;
	case LF_SCHEME:
		free(f->u.sch.name);
		free_stmts(f->u.sch.stmts, f->u.sch.n);
		break;
	case LF_MAPPING:
		free(f->u.map.name);
		free_stmts(f->u.map.archive, f->u.map.n_archive);
		free_stmts(f->u.map.entries, f->u.map.n_entries);
		break;
	case LF_FRAG_COND:
		free_frag_branch(f->u.cond.branches, f->u.cond.n);
		break;
	}
}

static void free_frag_branch(struct lf_frag_branch *v, int n)
{
	for (int i = 0; i < n; i++) {
		free(v[i].expr);
		for (int j = 0; j < v[i].n_frags; j++)
			free_frag(&v[i].frags[j]);
		free(v[i].frags);
	}
	free(v);
}

void lf_file_free(struct lf_file *f)
{
	if (!f)
		return;
	for (int i = 0; i < f->n_frags; i++)
		free_frag(&f->frags[i]);
	free(f->frags);
	free(f->path);
	free(f);
}

/* ================================================================== */
/*  Dump (debugging)                                                  */
/* ================================================================== */

static void dump_stmts(const struct lf_stmt *v, int n,
		       enum lf_frag_kind ctx, int depth);

static void pr_indent(int depth)
{
	for (int i = 0; i < depth; i++)
		printf("    ");
}

static void dump_entry(const struct lf_entry *e, enum lf_frag_kind ctx,
		       int depth)
{
	pr_indent(depth);
	switch (ctx) {
	case LF_SECTIONS:
		printf("%s\n", e->name);
		break;
	case LF_SCHEME:
		printf("%s -> %s\n", e->name, e->target);
		break;
	case LF_MAPPING:
		if (e->target)
			printf("%s:%s (%s)\n", e->name, e->target, e->scheme);
		else
			printf("%s (%s)\n", e->name, e->scheme);
		break;
	default:
		printf("%s\n", e->name);
		break;
	}
}

static void dump_cond(const struct lf_branch *branches, int nb,
		      enum lf_frag_kind ctx, int depth)
{
	for (int i = 0; i < nb; i++) {
		pr_indent(depth);
		if (branches[i].expr)
			printf("%s %s:\n", i == 0 ? "if" : "elif",
			       branches[i].expr);
		else
			printf("else:\n");
		dump_stmts(branches[i].stmts, branches[i].n_stmts,
			   ctx, depth + 1);
	}
}

static void dump_stmts(const struct lf_stmt *v, int n,
		       enum lf_frag_kind ctx, int depth)
{
	for (int i = 0; i < n; i++) {
		if (v[i].is_cond)
			dump_cond(v[i].u.cond.branches,
				  v[i].u.cond.n_branches, ctx, depth);
		else
			dump_entry(&v[i].u.entry, ctx, depth);
	}
}

static void dump_frag(const struct lf_frag *f, int depth);

static void dump_frag_cond(const struct lf_frag_branch *branches, int nb,
			   int depth)
{
	for (int i = 0; i < nb; i++) {
		pr_indent(depth);
		if (branches[i].expr)
			printf("%s %s:\n", i == 0 ? "if" : "elif",
			       branches[i].expr);
		else
			printf("else:\n");
		for (int j = 0; j < branches[i].n_frags; j++)
			dump_frag(&branches[i].frags[j], depth + 1);
	}
}

static void dump_frag(const struct lf_frag *f, int depth)
{
	switch (f->kind) {
	case LF_SECTIONS:
		pr_indent(depth);
		printf("[sections:%s]\n", f->u.sec.name);
		dump_stmts(f->u.sec.stmts, f->u.sec.n, LF_SECTIONS,
			   depth + 1);
		break;
	case LF_SCHEME:
		pr_indent(depth);
		printf("[scheme:%s]\n", f->u.sch.name);
		dump_stmts(f->u.sch.stmts, f->u.sch.n, LF_SCHEME,
			   depth + 1);
		break;
	case LF_MAPPING:
		pr_indent(depth);
		printf("[mapping:%s]\n", f->u.map.name);
		pr_indent(depth + 1);
		printf("archive:\n");
		dump_stmts(f->u.map.archive, f->u.map.n_archive,
			   LF_FRAG_COND, depth + 2);
		dump_stmts(f->u.map.entries, f->u.map.n_entries,
			   LF_MAPPING, depth + 1);
		break;
	case LF_FRAG_COND:
		dump_frag_cond(f->u.cond.branches, f->u.cond.n, depth);
		break;
	}
}

void lf_file_dump(const struct lf_file *f)
{
	printf("--- %s ---\n", f->path);
	for (int i = 0; i < f->n_frags; i++)
		dump_frag(&f->frags[i], 0);
}
