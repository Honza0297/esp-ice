/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file color.h
 * @brief Terminal color support.
 *
 * Colored output uses inline tokens in printf format strings:
 *
 *   fprintf(stderr, "@r{fatal}: %s\n", msg);
 *   printf("@g{done} in %d seconds\n", elapsed);
 *
 * Tokens:
 *   @r{...}  red          @R{...}  bold red
 *   @g{...}  green        @G{...}  bold green
 *   @y{...}  yellow       @Y{...}  bold yellow
 *   @b{...}  bold         @c{...}  cyan
 *   @@       literal @
 *   }}       literal } inside a color block
 *
 * The @x{...} tokens are expanded by the platform fprintf/vfprintf
 * overrides (platform/posix/io.c, platform/win/io.c). When color is
 * disabled (piped output or --no-color), tokens are stripped.
 *
 * On Windows, if ENABLE_VIRTUAL_TERMINAL_PROCESSING is available
 * (Windows 10 1511+), ANSI codes are emitted directly. On older
 * Windows, colors are rendered via the Console API
 * (SetConsoleTextAttribute) as a fallback.
 */
#ifndef COLOR_H
#define COLOR_H

#include <stddef.h>

struct sbuf;

/** Global color flag. Set by color_init(), cleared by --no-color. */
extern int use_color;

/**
 * Global VT processing flag. Set when the terminal handles ANSI
 * escape codes natively (POSIX always, Windows 10 1511+).
 * When false and use_color is true, the Console API fallback is used.
 */
extern int use_vt;

/** Raw ANSI escape codes (no runtime check -- used internally). */
#define COLOR_RESET       "\033[0m"
#define COLOR_BOLD        "\033[1m"
#define COLOR_RED         "\033[31m"
#define COLOR_GREEN       "\033[32m"
#define COLOR_YELLOW      "\033[33m"
#define COLOR_BLUE        "\033[34m"
#define COLOR_MAGENTA     "\033[35m"
#define COLOR_CYAN        "\033[36m"
#define COLOR_BOLD_RED    "\033[1;31m"
#define COLOR_BOLD_GREEN  "\033[1;32m"
#define COLOR_BOLD_YELLOW "\033[1;33m"

/**
 * @brief Initialize color support.
 *
 * Enables color if @p fd refers to a terminal (isatty).
 * On POSIX, also sets use_vt (always true).
 * On Windows, use_vt is set by wmain's setup_io() based on
 * whether ENABLE_VIRTUAL_TERMINAL_PROCESSING succeeds.
 * Call once at startup before any colored output.
 */
void color_init(int fd);

/**
 * @brief Expand @x{...} color tokens in a format string.
 *
 * When use_color is set, tokens are replaced with ANSI escape codes.
 * When unset, tokens are stripped and only the text content remains.
 * The expanded result is a valid printf format string.
 *
 * Called by the platform fprintf/vfprintf overrides.
 *
 * @param out  Destination sbuf for the expanded format string.
 * @param fmt  Format string with @x{...} tokens.
 */
void expand_colors(struct sbuf *out, const char *fmt);

#endif /* COLOR_H */
