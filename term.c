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

		/* @[sgr]{ -> arbitrary SGR code, e.g. @[38;5;208]{ */
		if (*fmt == '@' && fmt[1] == '[') {
			const char *end = strchr(fmt + 2, ']');
			if (end && end[1] == '{') {
				if (use_color) {
					sbuf_addstr(out, "\033[");
					sbuf_add(out, fmt + 2,
						 (size_t)(end - (fmt + 2)));
					sbuf_addch(out, 'm');
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
			case 'r': code = COLOR_RED; break;
			case 'g': code = COLOR_GREEN; break;
			case 'y': code = COLOR_YELLOW; break;
			case 'b': code = COLOR_BOLD; break;
			case 'c': code = COLOR_CYAN; break;
			case 'R': code = COLOR_BOLD_RED; break;
			case 'G': code = COLOR_BOLD_GREEN; break;
			case 'Y': code = COLOR_BOLD_YELLOW; break;
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
				sbuf_addstr(out, COLOR_RESET);
			depth--;
			fmt++;
			continue;
		}

		sbuf_addch(out, *fmt++);
	}
}
