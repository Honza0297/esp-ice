/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file term.c
 * @brief Terminal presentation support -- shared logic.
 *
 * Provides the color token expander (expand_colors) used by the
 * platform fprintf overrides, and the global color/VT flags.
 */
#include "ice.h"

int use_color;
int use_vt;

void color_init(int fd)
{
	use_color = isatty(fd);
#ifndef _WIN32
	/* POSIX terminals always support ANSI escape codes. */
	use_vt = 1;
#endif
}

/* Named color lookup for @[COLOR_RED]{...} syntax. */
static const struct {
	const char *name;
	const char *code;
} color_names[] = {
	{"COLOR_RESET",        "\033[0m"},
	{"COLOR_BOLD",         "\033[1m"},
	{"COLOR_UNDERLINE",    "\033[4m"},
	{"COLOR_REVERSE",      "\033[7m"},
	{"COLOR_BLACK",        "\033[30m"},
	{"COLOR_RED",          "\033[31m"},
	{"COLOR_GREEN",        "\033[32m"},
	{"COLOR_YELLOW",       "\033[33m"},
	{"COLOR_BLUE",         "\033[34m"},
	{"COLOR_MAGENTA",      "\033[35m"},
	{"COLOR_CYAN",         "\033[36m"},
	{"COLOR_WHITE",        "\033[37m"},
	{"COLOR_BOLD_RED",     "\033[1;31m"},
	{"COLOR_BOLD_GREEN",   "\033[1;32m"},
	{"COLOR_BOLD_YELLOW",  "\033[1;33m"},
	{"COLOR_BOLD_BLUE",    "\033[1;34m"},
	{"COLOR_BOLD_MAGENTA", "\033[1;35m"},
	{"COLOR_BOLD_CYAN",    "\033[1;36m"},
	{"COLOR_BOLD_WHITE",   "\033[1;37m"},
	{"COLOR_BG_RED",       "\033[41m"},
	{"COLOR_BG_GREEN",     "\033[42m"},
	{"COLOR_BG_YELLOW",    "\033[43m"},
	{"COLOR_BG_BLUE",      "\033[44m"},
	{"COLOR_BG_MAGENTA",   "\033[45m"},
	{"COLOR_BG_CYAN",      "\033[46m"},
	{"COLOR_BG_WHITE",     "\033[47m"},
};

static const char *find_color_name(const char *name, size_t len)
{
	size_t i;
	for (i = 0; i < sizeof(color_names) / sizeof(color_names[0]); i++) {
		if (strlen(color_names[i].name) == len &&
		    !memcmp(color_names[i].name, name, len))
			return color_names[i].code;
	}
	return NULL;
}

/**
 * @brief Expand @x{...} color tokens in a format string.
 *
 * When use_color is set, tokens are replaced with ANSI escape codes.
 * When unset, tokens are stripped and only the text content remains.
 * The expanded result is a valid printf format string.
 *
 * Escaping: @@ -> literal @, }} -> literal } inside a color block.
 */
void expand_colors(struct sbuf *out, const char *fmt)
{
	int depth = 0;

	while (*fmt) {
		/* @@ -> literal @ */
		if (*fmt == '@' && fmt[1] == '@') {
			sbuf_addch(out, '@');
			fmt += 2;
			continue;
		}

		/*
		 * @[spec]{ -> named color or numeric SGR:
		 *   @[COLOR_RED]{...}    named lookup
		 *   @[38;5;208]{...}     raw SGR parameters
		 */
		if (*fmt == '@' && fmt[1] == '[') {
			const char *end = strchr(fmt + 2, ']');
			if (end && end[1] == '{') {
				if (use_color) {
					const char *s = fmt + 2;
					size_t len = (size_t)(end - s);
					const char *code =
						find_color_name(s, len);
					if (code) {
						sbuf_addstr(out, code);
					} else {
						sbuf_addstr(out, "\033[");
						sbuf_add(out, s, len);
						sbuf_addch(out, 'm');
					}
				}
				fmt = end + 2;
				depth++;
				continue;
			}
		}

		/* @x{ -> start color (only for recognized letters) */
		if (*fmt == '@' && fmt[1] && fmt[2] == '{') {
			const char *code = NULL;

			switch (fmt[1]) {
			case 'r': code = "\033[31m"; break;
			case 'g': code = "\033[32m"; break;
			case 'y': code = "\033[33m"; break;
			case 'b': code = "\033[1m"; break;
			case 'c': code = "\033[36m"; break;
			case 'R': code = "\033[1;31m"; break;
			case 'G': code = "\033[1;32m"; break;
			case 'Y': code = "\033[1;33m"; break;
			}

			if (code) {
				if (use_color)
					sbuf_addstr(out, code);
				fmt += 3;
				depth++;
				continue;
			}
			/* Unrecognized: fall through, emit '@' as literal */
		}

		/* }} -> literal } when inside a color block */
		if (*fmt == '}' && fmt[1] == '}' && depth > 0) {
			sbuf_addch(out, '}');
			fmt += 2;
			continue;
		}

		/* } -> close color block */
		if (*fmt == '}' && depth > 0) {
			if (use_color)
				sbuf_addstr(out, "\033[0m");
			depth--;
			fmt++;
			continue;
		}

		sbuf_addch(out, *fmt++);
	}
}
