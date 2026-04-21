/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for the cconfig Kconfig lexer.
 */
#include "ice.h"
#include "cconfig/cconfig.h"
#include "tap.h"

static void expect(struct kc_lexer *lex, enum kc_token_type type,
		   const char *value)
{
	struct kc_token tok;
	kc_lexer_next(lex, &tok);
	if (tok.type != type) {
		printf("# expected type %s, got %s",
		       kc_token_type_name(type),
		       kc_token_type_name(tok.type));
		if (tok.value)
			printf(" (value=\"%s\")", tok.value);
		printf(" at %d:%d\n", tok.line, tok.col);
		tap_test_pass = 0;
	} else if (value && (!tok.value || strcmp(tok.value, value) != 0)) {
		printf("# expected value \"%s\", got \"%s\" at %d:%d\n",
		       value, tok.value ? tok.value : "(null)",
		       tok.line, tok.col);
		tap_test_pass = 0;
	}
	kc_token_release(&tok);
}

static void expect_at(struct kc_lexer *lex, enum kc_token_type type,
		      const char *value, int line, int col)
{
	struct kc_token tok;
	kc_lexer_next(lex, &tok);
	if (tok.type != type) {
		printf("# expected type %s, got %s at %d:%d\n",
		       kc_token_type_name(type),
		       kc_token_type_name(tok.type),
		       tok.line, tok.col);
		tap_test_pass = 0;
	} else if (value && (!tok.value || strcmp(tok.value, value) != 0)) {
		printf("# expected value \"%s\", got \"%s\" at %d:%d\n",
		       value, tok.value ? tok.value : "(null)",
		       tok.line, tok.col);
		tap_test_pass = 0;
	}
	if (tok.line != line || tok.col != col) {
		printf("# expected position %d:%d, got %d:%d\n",
		       line, col, tok.line, tok.col);
		tap_test_pass = 0;
	}
	kc_token_release(&tok);
}

int main(void)
{
	/* Empty input returns EOF immediately. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "", "test");
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("empty input returns EOF");
	}

	/* Basic: config FOO */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "config FOO\n", "test");
		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_WORD, "FOO");
		expect(&lex, KC_TOK_NEWLINE, "\n");
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("basic: config FOO");
	}

	/* All keyword types (except help, tested separately). */
	{
		struct kc_lexer lex;
		static const char *kw_input =
			"mainmenu\nmenu\nendmenu\nconfig\nmenuconfig\n"
			"choice\nendchoice\nif\nendif\ncomment\n"
			"source\nrsource\nosource\norsource\n"
			"depends\non\ndefault\nselect\nimply\nrange\n"
			"bool\nint\nstring\nhex\nfloat\ntristate\n"
			"prompt\nvisible\noptional\nset\nwarning\n"
			"def_bool\ndef_int\ndef_hex\ndef_string\n"
			"def_tristate\ndef_float\n";

		kc_lexer_init(&lex, kw_input, "test");

		expect(&lex, KC_TOK_MAINMENU, "mainmenu");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_MENU, "menu");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_ENDMENU, "endmenu");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_MENUCONFIG, "menuconfig");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_CHOICE, "choice");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_ENDCHOICE, "endchoice");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_IF, "if");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_ENDIF, "endif");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_COMMENT, "comment");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_SOURCE, "source");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_RSOURCE, "rsource");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_OSOURCE, "osource");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_ORSOURCE, "orsource");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEPENDS, "depends");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_ON, "on");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEFAULT, "default");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_SELECT, "select");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_IMPLY, "imply");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_RANGE, "range");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_BOOL, "bool");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_INT, "int");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_STRING, "string");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_HEX, "hex");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_FLOAT, "float");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_TRISTATE, "tristate");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_PROMPT, "prompt");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_VISIBLE, "visible");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_OPTIONAL, "optional");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_SET, "set");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_WARNING, "warning");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEF_BOOL, "def_bool");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEF_INT, "def_int");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEF_HEX, "def_hex");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEF_STRING, "def_string");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEF_TRISTATE, "def_tristate");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_DEF_FLOAT, "def_float");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);

		tap_done("all keyword types recognized");
	}

	/* help keyword triggers help_mode. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex,
			      "help\n"
			      "  some text\n"
			      "done\n",
			      "test");
		expect(&lex, KC_TOK_HELP, "help");
		expect(&lex, KC_TOK_HELP_TEXT, "some text");
		expect(&lex, KC_TOK_WORD, "done");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("help keyword recognized and triggers help_mode");
	}

	/* All operators. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "= != < > <= >= && || ! ( )\n", "test");
		expect(&lex, KC_TOK_ASSIGN, "=");
		expect(&lex, KC_TOK_NOT_EQUAL, "!=");
		expect(&lex, KC_TOK_LESS, "<");
		expect(&lex, KC_TOK_GREATER, ">");
		expect(&lex, KC_TOK_LESS_EQ, "<=");
		expect(&lex, KC_TOK_GREATER_EQ, ">=");
		expect(&lex, KC_TOK_AND, "&&");
		expect(&lex, KC_TOK_OR, "||");
		expect(&lex, KC_TOK_NOT, "!");
		expect(&lex, KC_TOK_LPAREN, "(");
		expect(&lex, KC_TOK_RPAREN, ")");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("all operators");
	}

	/* Double-quoted string with escapes. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "\"hello\\nworld\\t\\\"end\\\\\"", "test");
		expect(&lex, KC_TOK_QUOTED, "hello\nworld\t\"end\\");
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("double-quoted string with escapes");
	}

	/* Single-quoted string. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "'hello world'\n", "test");
		expect(&lex, KC_TOK_QUOTED, "hello world");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("single-quoted string");
	}

	/* Multi-line input: verify line numbers. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex,
			      "config FOO\n"
			      "  bool\n"
			      "  default y\n",
			      "test");

		expect_at(&lex, KC_TOK_CONFIG, "config", 1, 1);
		expect_at(&lex, KC_TOK_WORD, "FOO", 1, 8);
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect_at(&lex, KC_TOK_BOOL, "bool", 2, 3);
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect_at(&lex, KC_TOK_DEFAULT, "default", 3, 3);
		expect_at(&lex, KC_TOK_WORD, "y", 3, 11);
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("multi-line input with correct line numbers");
	}

	/* Help text. */
	{
		struct kc_lexer lex;
		struct kc_token tok;
		kc_lexer_init(&lex,
			      "config FOO\n"
			      "  help\n"
			      "    This is the first line.\n"
			      "    And the second line.\n"
			      "\n"
			      "    Third after blank.\n"
			      "  bool\n",
			      "test");

		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_WORD, "FOO");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_HELP, "help");

		kc_lexer_next(&lex, &tok);
		tap_check(tok.type == KC_TOK_HELP_TEXT);
		tap_check(tok.value != NULL);
		if (tok.value) {
			tap_check(strcmp(tok.value,
					"This is the first line.\n"
					"And the second line.\n"
					"\n"
					"Third after blank.") == 0);
		}
		kc_token_release(&tok);

		expect(&lex, KC_TOK_BOOL, "bool");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("help text block");
	}

	/* Comments: lines starting with # are skipped. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex,
			      "# This is a comment\n"
			      "config BAR\n"
			      "  # another comment\n"
			      "  bool\n",
			      "test");

		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_WORD, "BAR");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_BOOL, "bool");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("comments are skipped");
	}

	/* Expression: depends on A && (B || !C) */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "depends on A && (B || !C)\n", "test");
		expect(&lex, KC_TOK_DEPENDS, "depends");
		expect(&lex, KC_TOK_ON, "on");
		expect(&lex, KC_TOK_WORD, "A");
		expect(&lex, KC_TOK_AND, "&&");
		expect(&lex, KC_TOK_LPAREN, "(");
		expect(&lex, KC_TOK_WORD, "B");
		expect(&lex, KC_TOK_OR, "||");
		expect(&lex, KC_TOK_NOT, "!");
		expect(&lex, KC_TOK_WORD, "C");
		expect(&lex, KC_TOK_RPAREN, ")");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("expression: depends on A && (B || !C)");
	}

	/* Identifiers vs keywords: unknown words are KC_TOK_WORD. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "config MY_VAR_123\n", "test");
		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_WORD, "MY_VAR_123");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("identifiers with digits and underscores");
	}

	/* Consecutive newlines are collapsed. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "config A\n\n\n\nconfig B\n", "test");
		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_WORD, "A");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_WORD, "B");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("consecutive newlines collapsed");
	}

	/* Whitespace-only input returns EOF. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "   \t  ", "test");
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("whitespace-only input returns EOF");
	}

	/* select / imply with if guard. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "select FOO if BAR\n", "test");
		expect(&lex, KC_TOK_SELECT, "select");
		expect(&lex, KC_TOK_WORD, "FOO");
		expect(&lex, KC_TOK_IF, "if");
		expect(&lex, KC_TOK_WORD, "BAR");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("select with if guard");
	}

	/* kc_token_type_name covers all types. */
	{
		tap_check(strcmp(kc_token_type_name(KC_TOK_EOF), "EOF") == 0);
		tap_check(strcmp(kc_token_type_name(KC_TOK_NEWLINE), "NEWLINE") == 0);
		tap_check(strcmp(kc_token_type_name(KC_TOK_WORD), "WORD") == 0);
		tap_check(strcmp(kc_token_type_name(KC_TOK_QUOTED), "QUOTED") == 0);
		tap_check(strcmp(kc_token_type_name(KC_TOK_CONFIG), "CONFIG") == 0);
		tap_check(strcmp(kc_token_type_name(KC_TOK_AND), "AND") == 0);
		tap_check(strcmp(kc_token_type_name(KC_TOK_RPAREN), "RPAREN") == 0);
		tap_done("kc_token_type_name returns correct names");
	}

	/* Source directives. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "source \"Kconfig.soc\"\n", "test");
		expect(&lex, KC_TOK_SOURCE, "source");
		expect(&lex, KC_TOK_QUOTED, "Kconfig.soc");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("source directive with quoted path");
	}

	/* range operator. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "range 0 100\n", "test");
		expect(&lex, KC_TOK_RANGE, "range");
		expect(&lex, KC_TOK_WORD, "0");
		expect(&lex, KC_TOK_WORD, "100");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("range with numeric values");
	}

	/* Unterminated quoted string emits KC_TOK_ERROR. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "\"no closing quote\n", "test");
		expect(&lex, KC_TOK_ERROR, "no closing quote\n");
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("unterminated quoted string emits ERROR");
	}

	/* Unknown character emits KC_TOK_ERROR. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "config @FOO\n", "test");
		expect(&lex, KC_TOK_CONFIG, "config");
		expect(&lex, KC_TOK_ERROR, "@");
		expect(&lex, KC_TOK_WORD, "FOO");
		expect(&lex, KC_TOK_NEWLINE, NULL);
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("unknown character emits ERROR");
	}

	/* help at EOF with no content. */
	{
		struct kc_lexer lex;
		kc_lexer_init(&lex, "help\n", "test");
		expect(&lex, KC_TOK_HELP, "help");
		expect(&lex, KC_TOK_HELP_TEXT, "");
		expect(&lex, KC_TOK_EOF, NULL);
		tap_done("help at EOF with no content");
	}

	/* kc_token_release called twice is safe. */
	{
		struct kc_lexer lex;
		struct kc_token tok;
		kc_lexer_init(&lex, "config\n", "test");
		kc_lexer_next(&lex, &tok);
		tap_check(tok.type == KC_TOK_CONFIG);
		kc_token_release(&tok);
		tap_check(tok.value == NULL);
		kc_token_release(&tok);
		tap_check(tok.value == NULL);
		tap_done("kc_token_release called twice is safe");
	}

	return tap_result();
}
