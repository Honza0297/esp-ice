/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lexer.c
 * @brief Kconfig lexer — scans a NUL-terminated buffer into tokens.
 *
 * The lexer produces one token at a time via kc_lexer_next().  Tokens
 * own their value string; callers must free it with kc_token_release().
 *
 * Help text is special-cased: after emitting KC_TOK_HELP, the lexer
 * enters help_mode and the next call returns a single KC_TOK_HELP_TEXT
 * token whose value is the full help paragraph (terminated by
 * dedentation).
 */
#include "cconfig/cconfig.h"
#include "ice.h"

/* ------------------------------------------------------------------ */
/*  Keyword table                                                      */
/* ------------------------------------------------------------------ */

struct kw_entry {
	const char *word;
	enum kc_token_type type;
};

/* Must stay sorted alphabetically for binary search in lookup_keyword(). */
static const struct kw_entry kw_table[] = {
    {"bool", KC_TOK_BOOL},
    {"choice", KC_TOK_CHOICE},
    {"comment", KC_TOK_COMMENT},
    {"config", KC_TOK_CONFIG},
    {"def_bool", KC_TOK_DEF_BOOL},
    {"def_float", KC_TOK_DEF_FLOAT},
    {"def_hex", KC_TOK_DEF_HEX},
    {"def_int", KC_TOK_DEF_INT},
    {"def_string", KC_TOK_DEF_STRING},
    {"def_tristate", KC_TOK_DEF_TRISTATE},
    {"default", KC_TOK_DEFAULT},
    {"depends", KC_TOK_DEPENDS},
    {"endchoice", KC_TOK_ENDCHOICE},
    {"endif", KC_TOK_ENDIF},
    {"endmenu", KC_TOK_ENDMENU},
    {"float", KC_TOK_FLOAT},
    {"help", KC_TOK_HELP},
    {"hex", KC_TOK_HEX},
    {"if", KC_TOK_IF},
    {"imply", KC_TOK_IMPLY},
    {"int", KC_TOK_INT},
    {"mainmenu", KC_TOK_MAINMENU},
    {"menu", KC_TOK_MENU},
    {"menuconfig", KC_TOK_MENUCONFIG},
    {"on", KC_TOK_ON},
    {"option", KC_TOK_OPTION},
    {"optional", KC_TOK_OPTIONAL},
    {"orsource", KC_TOK_ORSOURCE},
    {"osource", KC_TOK_OSOURCE},
    {"prompt", KC_TOK_PROMPT},
    {"range", KC_TOK_RANGE},
    {"rsource", KC_TOK_RSOURCE},
    {"select", KC_TOK_SELECT},
    {"set", KC_TOK_SET},
    {"source", KC_TOK_SOURCE},
    {"string", KC_TOK_STRING},
    {"tristate", KC_TOK_TRISTATE},
    {"visible", KC_TOK_VISIBLE},
    {"warning", KC_TOK_WARNING},
};

#define KW_COUNT (sizeof(kw_table) / sizeof(kw_table[0]))

static enum kc_token_type lookup_keyword(const char *word)
{
	size_t lo = 0;
	size_t hi = KW_COUNT;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		int cmp = strcmp(word, kw_table[mid].word);
		if (cmp == 0)
			return kw_table[mid].type;
		if (cmp < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return KC_TOK_WORD;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
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
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void kc_lexer_init(struct kc_lexer *lex, const char *input, const char *file)
{
	lex->input = input;
	lex->pos = input;
	lex->file = file;
	lex->line = 1;
	lex->col = 1;
	lex->help_mode = 0;
}

/**
 * Fill a token with the given type, file, line, column, and a
 * dynamically allocated copy of @p value.
 */
static void emit(struct kc_token *tok, enum kc_token_type type,
		 const char *file, int line, int col, const char *value)
{
	tok->type = type;
	tok->file = file;
	tok->line = line;
	tok->col = col;
	tok->value = value ? sbuf_strdup(value) : NULL;
}

/**
 * Scan help text after a `help` keyword.
 *
 * When entering this function, lex->pos sits just past "help" (likely
 * on the newline).  We skip past that newline, then:
 *
 *   1. The first non-blank line's indentation becomes @c base_indent.
 *   2. All subsequent lines that are blank or whose indentation is
 *      >= @c base_indent are part of the help text.
 *   3. The block ends at the first non-blank line with indentation
 *      < @c base_indent, or at EOF.
 *
 * Each collected line has its leading @c base_indent characters
 * stripped.  Trailing blank lines are trimmed from the result.
 */
static void scan_help_text(struct kc_lexer *lex, struct kc_token *tok)
{
	struct sbuf sb = SBUF_INIT;
	const char *ptr = lex->pos;
	int start_line;
	int base_indent = -1;

	/* Skip past the newline after "help". */
	while (*ptr == ' ' || *ptr == '\t')
		ptr++;
	if (*ptr == '\n') {
		ptr++;
		lex->line++;
	}

	start_line = lex->line;

	while (*ptr != '\0') {
		const char *line_start = ptr;
		int indent = 0;

		while (*ptr == ' ' || *ptr == '\t') {
			indent++;
			ptr++;
		}

		/* Blank line: always part of help text. */
		if (*ptr == '\n') {
			ptr++;
			lex->line++;
			sbuf_addch(&sb, '\n');
			continue;
		}

		/* EOF on a whitespace-only trailing line. */
		if (*ptr == '\0') {
			sbuf_addch(&sb, '\n');
			break;
		}

		/* First non-blank line establishes the base indent. */
		if (base_indent < 0)
			base_indent = indent;

		/* Dedent: this line is no longer part of help. */
		if (indent < base_indent) {
			ptr = line_start;
			break;
		}

		/* Append this line with base indent stripped. */
		{
			const char *content = line_start + base_indent;
			const char *eol = content;
			while (*eol != '\n' && *eol != '\0')
				eol++;
			sbuf_add(&sb, content, (size_t)(eol - content));
			sbuf_addch(&sb, '\n');
			if (*eol == '\n')
				eol++;
			ptr = eol;
			lex->line++;
		}
	}

	/* Trim trailing newlines via sbuf_setlen. */
	{
		size_t trim = sb.len;
		while (trim > 0 && sb.buf[trim - 1] == '\n')
			trim--;
		sbuf_setlen(&sb, trim);
	}

	lex->pos = ptr;
	lex->col = 1;
	lex->help_mode = 0;

	tok->type = KC_TOK_HELP_TEXT;
	tok->file = lex->file;
	tok->line = start_line;
	tok->col = 1;
	tok->value = sbuf_detach(&sb);
}

static void scan_quoted_string(struct kc_lexer *lex, struct kc_token *tok,
			       char quote)
{
	struct sbuf sb = SBUF_INIT;
	int start_line = lex->line;
	int start_col = lex->col;
	const char *ptr = lex->pos;

	ptr++; /* skip opening quote */
	lex->col++;

	while (*ptr != '\0' && *ptr != quote) {
		if (*ptr == '\\' && ptr[1] != '\0') {
			ptr++;
			lex->col++;
			switch (*ptr) {
			case 'n':
				sbuf_addch(&sb, '\n');
				break;
			case 't':
				sbuf_addch(&sb, '\t');
				break;
			case '\\':
				sbuf_addch(&sb, '\\');
				break;
			case '"':
				sbuf_addch(&sb, '"');
				break;
			case '\'':
				sbuf_addch(&sb, '\'');
				break;
			default:
				sbuf_addch(&sb, '\\');
				sbuf_addch(&sb, *ptr);
				break;
			}
			ptr++;
			lex->col++;
		} else {
			if (*ptr == '\n') {
				lex->line++;
				lex->col = 1;
			} else {
				lex->col++;
			}
			sbuf_addch(&sb, *ptr);
			ptr++;
		}
	}

	lex->pos = ptr;

	if (*ptr == quote) {
		lex->pos++; /* skip closing quote */
		lex->col++;
		tok->type = KC_TOK_QUOTED;
	} else {
		tok->type = KC_TOK_ERROR;
	}
	tok->file = lex->file;
	tok->line = start_line;
	tok->col = start_col;
	tok->value = sbuf_detach(&sb);
}

void kc_lexer_next(struct kc_lexer *lex, struct kc_token *tok)
{
	const char *ptr;

	/* Help mode: collect the indented help block. */
	if (lex->help_mode) {
		scan_help_text(lex, tok);
		return;
	}

	ptr = lex->pos;

	/* Skip horizontal whitespace. */
	while (*ptr == ' ' || *ptr == '\t') {
		lex->col++;
		ptr++;
	}
	lex->pos = ptr;

	/* EOF */
	if (*ptr == '\0') {
		emit(tok, KC_TOK_EOF, lex->file, lex->line, lex->col, NULL);
		return;
	}

	/* Comment: skip to end of line, then emit newline. */
	if (*ptr == '#') {
		while (*ptr != '\0' && *ptr != '\n')
			ptr++;
		if (*ptr == '\n')
			ptr++;
		lex->pos = ptr;
		lex->line++;
		lex->col = 1;

		/* Collapse consecutive newlines / comment lines. */
		while (*lex->pos == '\n' || *lex->pos == '#') {
			if (*lex->pos == '#') {
				while (*lex->pos != '\0' && *lex->pos != '\n')
					lex->pos++;
			}
			if (*lex->pos == '\n') {
				lex->pos++;
				lex->line++;
			}
		}
		lex->col = 1;
		/*
		 * After collapsing a run of comment/blank lines, line - 1
		 * is the last consumed line, not the originating one.
		 */
		emit(tok, KC_TOK_NEWLINE, lex->file, lex->line - 1, 1, "\n");
		return;
	}

	/* Newline — collapse consecutive blank lines. */
	if (*ptr == '\n') {
		int nl_line = lex->line;
		ptr++;
		lex->line++;

		/* Collapse blank lines (whitespace-only before next \n). */
		for (;;) {
			const char *probe = ptr;
			while (*probe == ' ' || *probe == '\t')
				probe++;
			if (*probe != '\n')
				break;
			ptr = probe + 1;
			lex->line++;
		}

		lex->pos = ptr;
		lex->col = 1;
		emit(tok, KC_TOK_NEWLINE, lex->file, nl_line, 1, "\n");
		return;
	}

	/* Quoted string */
	if (*ptr == '"' || *ptr == '\'') {
		scan_quoted_string(lex, tok, *ptr);
		return;
	}

	/* Two-character operators */
	if (ptr[0] == ':' && ptr[1] == '=') {
		emit(tok, KC_TOK_COLON_ASSIGN, lex->file, lex->line, lex->col,
		     ":=");
		lex->pos = ptr + 2;
		lex->col += 2;
		return;
	}
	if (ptr[0] == '!' && ptr[1] == '=') {
		emit(tok, KC_TOK_NOT_EQUAL, lex->file, lex->line, lex->col,
		     "!=");
		lex->pos = ptr + 2;
		lex->col += 2;
		return;
	}
	if (ptr[0] == '<' && ptr[1] == '=') {
		emit(tok, KC_TOK_LESS_EQ, lex->file, lex->line, lex->col, "<=");
		lex->pos = ptr + 2;
		lex->col += 2;
		return;
	}
	if (ptr[0] == '>' && ptr[1] == '=') {
		emit(tok, KC_TOK_GREATER_EQ, lex->file, lex->line, lex->col,
		     ">=");
		lex->pos = ptr + 2;
		lex->col += 2;
		return;
	}
	if (ptr[0] == '&' && ptr[1] == '&') {
		emit(tok, KC_TOK_AND, lex->file, lex->line, lex->col, "&&");
		lex->pos = ptr + 2;
		lex->col += 2;
		return;
	}
	if (ptr[0] == '|' && ptr[1] == '|') {
		emit(tok, KC_TOK_OR, lex->file, lex->line, lex->col, "||");
		lex->pos = ptr + 2;
		lex->col += 2;
		return;
	}

	/* Single-character operators */
	{
		enum kc_token_type op_type = KC_TOK_EOF;
		switch (*ptr) {
		case '=':
			op_type = KC_TOK_ASSIGN;
			break;
		case '!':
			op_type = KC_TOK_NOT;
			break;
		case '<':
			op_type = KC_TOK_LESS;
			break;
		case '>':
			op_type = KC_TOK_GREATER;
			break;
		case '(':
			op_type = KC_TOK_LPAREN;
			break;
		case ')':
			op_type = KC_TOK_RPAREN;
			break;
		default:
			break;
		}
		if (op_type != KC_TOK_EOF) {
			char val[2] = {*ptr, '\0'};
			emit(tok, op_type, lex->file, lex->line, lex->col, val);
			lex->pos = ptr + 1;
			lex->col++;
			return;
		}
	}

	/* Dollar-sign variable reference: $(NAME), ${NAME}, or $NAME */
	if (*ptr == '$') {
		const char *start = ptr;
		int start_col = lex->col;
		char *word;

		ptr++;
		lex->col++;
		if (*ptr == '(') {
			ptr++;
			lex->col++;
			while (*ptr && *ptr != ')' && *ptr != '\n') {
				ptr++;
				lex->col++;
			}
			if (*ptr == ')') {
				ptr++;
				lex->col++;
			}
		} else if (*ptr == '{') {
			ptr++;
			lex->col++;
			while (*ptr && *ptr != '}' && *ptr != '\n') {
				ptr++;
				lex->col++;
			}
			if (*ptr == '}') {
				ptr++;
				lex->col++;
			}
		} else if (is_ident_start(*ptr)) {
			while (is_ident_char(*ptr)) {
				ptr++;
				lex->col++;
			}
		}

		word = sbuf_strndup(start, (size_t)(ptr - start));
		lex->pos = ptr;

		tok->type = KC_TOK_WORD;
		tok->file = lex->file;
		tok->line = lex->line;
		tok->col = start_col;
		tok->value = word;
		return;
	}

	/* Negative number: -<digit>... (including floats like -1.5) */
	if (*ptr == '-' && ptr[1] >= '0' && ptr[1] <= '9') {
		const char *start = ptr;
		int start_col = lex->col;
		char *word;

		ptr++;
		lex->col++;
		while (is_ident_char(*ptr) || *ptr == '.') {
			ptr++;
			lex->col++;
		}

		word = sbuf_strndup(start, (size_t)(ptr - start));
		lex->pos = ptr;

		tok->type = KC_TOK_WORD;
		tok->file = lex->file;
		tok->line = lex->line;
		tok->col = start_col;
		tok->value = word;
		return;
	}

	/* Identifier / keyword / numeric literal (floats like 5.3) */
	if (is_ident_start(*ptr) || (*ptr >= '0' && *ptr <= '9')) {
		const char *start = ptr;
		int start_col = lex->col;
		int is_numeric = (*ptr >= '0' && *ptr <= '9');
		enum kc_token_type kw_type;
		char *word;

		while (is_ident_char(*ptr) || (is_numeric && *ptr == '.')) {
			ptr++;
			lex->col++;
		}

		word = sbuf_strndup(start, (size_t)(ptr - start));
		lex->pos = ptr;

		kw_type = is_numeric ? KC_TOK_WORD : lookup_keyword(word);
		tok->type = kw_type;
		tok->file = lex->file;
		tok->line = lex->line;
		tok->col = start_col;
		tok->value = word;

		if (kw_type == KC_TOK_HELP)
			lex->help_mode = 1;

		return;
	}

	/* Unknown character: emit error token. */
	{
		char val[2] = {*ptr, '\0'};
		emit(tok, KC_TOK_ERROR, lex->file, lex->line, lex->col, val);
		lex->pos = ptr + 1;
		lex->col++;
	}
}

void kc_token_release(struct kc_token *tok)
{
	free((void *)tok->value);
	tok->value = NULL;
}

/* Indexed by enum kc_token_type; must stay in sync with the enum. */
static const char *const tok_names[] = {
    [KC_TOK_EOF] = "EOF",
    [KC_TOK_NEWLINE] = "NEWLINE",
    [KC_TOK_WORD] = "WORD",
    [KC_TOK_QUOTED] = "QUOTED",
    [KC_TOK_HELP_TEXT] = "HELP_TEXT",
    [KC_TOK_MAINMENU] = "MAINMENU",
    [KC_TOK_MENU] = "MENU",
    [KC_TOK_ENDMENU] = "ENDMENU",
    [KC_TOK_CONFIG] = "CONFIG",
    [KC_TOK_MENUCONFIG] = "MENUCONFIG",
    [KC_TOK_CHOICE] = "CHOICE",
    [KC_TOK_ENDCHOICE] = "ENDCHOICE",
    [KC_TOK_IF] = "IF",
    [KC_TOK_ENDIF] = "ENDIF",
    [KC_TOK_COMMENT] = "COMMENT",
    [KC_TOK_SOURCE] = "SOURCE",
    [KC_TOK_RSOURCE] = "RSOURCE",
    [KC_TOK_OSOURCE] = "OSOURCE",
    [KC_TOK_ORSOURCE] = "ORSOURCE",
    [KC_TOK_DEPENDS] = "DEPENDS",
    [KC_TOK_ON] = "ON",
    [KC_TOK_DEFAULT] = "DEFAULT",
    [KC_TOK_SELECT] = "SELECT",
    [KC_TOK_IMPLY] = "IMPLY",
    [KC_TOK_RANGE] = "RANGE",
    [KC_TOK_BOOL] = "BOOL",
    [KC_TOK_INT] = "INT",
    [KC_TOK_STRING] = "STRING",
    [KC_TOK_HEX] = "HEX",
    [KC_TOK_FLOAT] = "FLOAT",
    [KC_TOK_TRISTATE] = "TRISTATE",
    [KC_TOK_PROMPT] = "PROMPT",
    [KC_TOK_HELP] = "HELP",
    [KC_TOK_VISIBLE] = "VISIBLE",
    [KC_TOK_OPTIONAL] = "OPTIONAL",
    [KC_TOK_SET] = "SET",
    [KC_TOK_WARNING] = "WARNING",
    [KC_TOK_OPTION] = "OPTION",
    [KC_TOK_DEF_BOOL] = "DEF_BOOL",
    [KC_TOK_DEF_INT] = "DEF_INT",
    [KC_TOK_DEF_HEX] = "DEF_HEX",
    [KC_TOK_DEF_STRING] = "DEF_STRING",
    [KC_TOK_DEF_TRISTATE] = "DEF_TRISTATE",
    [KC_TOK_DEF_FLOAT] = "DEF_FLOAT",
    [KC_TOK_ASSIGN] = "ASSIGN",
    [KC_TOK_COLON_ASSIGN] = "COLON_ASSIGN",
    [KC_TOK_NOT_EQUAL] = "NOT_EQUAL",
    [KC_TOK_LESS] = "LESS",
    [KC_TOK_GREATER] = "GREATER",
    [KC_TOK_LESS_EQ] = "LESS_EQ",
    [KC_TOK_GREATER_EQ] = "GREATER_EQ",
    [KC_TOK_AND] = "AND",
    [KC_TOK_OR] = "OR",
    [KC_TOK_NOT] = "NOT",
    [KC_TOK_LPAREN] = "LPAREN",
    [KC_TOK_RPAREN] = "RPAREN",
    [KC_TOK_ERROR] = "ERROR",
};

const char *kc_token_type_name(enum kc_token_type type)
{
	if ((unsigned)type < sizeof(tok_names) / sizeof(tok_names[0]) &&
	    tok_names[type])
		return tok_names[type];
	return "UNKNOWN";
}
