/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file term.h
 * @brief Terminal presentation support (colors, box-drawing).
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
 *   @[sgr]{...}     numeric SGR, e.g. @[38;5;208]{orange}
 *   @[name]{...}    named color, e.g. @[COLOR_RED]{red}
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
#ifndef TERM_H
#define TERM_H

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

/* Box-drawing characters (UTF-8) matching Rich's heavy-head style. */
#define TL "\xe2\x94\x8f" /* ┏ */
#define TM "\xe2\x94\xb3" /* ┳ */
#define TR "\xe2\x94\x93" /* ┓ */
#define ML "\xe2\x94\xa1" /* ┡ */
#define MM "\xe2\x95\x87" /* ╇ */
#define MR "\xe2\x94\xa9" /* ┩ */
#define BL "\xe2\x94\x94" /* └ */
#define BM "\xe2\x94\xb4" /* ┴ */
#define BR "\xe2\x94\x98" /* ┘ */
#define HH "\xe2\x94\x81" /* ━ heavy horizontal */
#define HL "\xe2\x94\x80" /* ─ light horizontal */
#define VH "\xe2\x94\x83" /* ┃ heavy vertical */
#define VL "\xe2\x94\x82" /* │ light vertical */

#endif /* TERM_H */
