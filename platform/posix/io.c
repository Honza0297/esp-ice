/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file platform/posix/io.c
 * @brief Color-aware I/O overrides for POSIX.
 *
 * IMPORTANT: This file captures the real C-library fputs pointer
 * before ice.h overrides it. ice.h is NOT the first include.
 * (Same pattern as platform/win/wconv.c -- see its file-level comment.)
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/** Real C-library fputs, captured before platform.h overrides it. */
static int (*real_fputs)(const char *, FILE *) = fputs;

#include "ice.h"

/**
 * @brief Color-aware vfprintf for POSIX.
 *
 * Substitutes conversion specifiers first, then expands @x{...}
 * tokens on the substituted result.  This ordering lets tokens in
 * %s arguments participate in expansion (and in the nesting stack)
 * instead of being spliced in literally after expand_colors runs.
 */
int vfprintf_p(FILE *stream, const char *fmt, va_list ap)
{
	struct sbuf formatted = SBUF_INIT;
	int n;

	sbuf_vaddf(&formatted, fmt, ap);

	if (memchr(formatted.buf, '@', formatted.len)) {
		struct sbuf expanded = SBUF_INIT;

		expand_colors(&expanded, formatted.buf, use_color_for(stream));
		n = real_fputs(expanded.buf, stream);
		sbuf_release(&expanded);
	} else {
		n = real_fputs(formatted.buf, stream);
	}

	sbuf_release(&formatted);
	return n;
}

/** Color-aware fprintf for POSIX. Delegates to vfprintf_p(). */
int fprintf_p(FILE *stream, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vfprintf_p(stream, fmt, ap);
	va_end(ap);
	return n;
}

int term_width(int fd)
{
	struct winsize ws;

	if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return ws.ws_col;
	return 80;
}

/** Color-aware fputs for POSIX. Expands @x{...} tokens. */
int fputs_p(const char *s, FILE *stream)
{
	struct sbuf expanded = SBUF_INIT;
	int n;

	if (!memchr(s, '@', strlen(s)))
		return real_fputs(s, stream);

	expand_colors(&expanded, s, use_color_for(stream));
	n = real_fputs(expanded.buf, stream);
	sbuf_release(&expanded);
	return n;
}

int is_directory(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int dir_foreach(const char *path, int (*cb)(const char *name, void *ud),
		void *ud)
{
	DIR *dir;
	struct dirent *de;
	struct svec names = SVEC_INIT;
	int rc = 0;

	dir = opendir(path);
	if (!dir)
		return -1;

	while ((de = readdir(dir)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		svec_push(&names, de->d_name);
	}
	closedir(dir);

	for (size_t i = 0; i < names.nr; i++) {
		rc = cb(names.v[i], ud);
		if (rc)
			break;
	}
	svec_clear(&names);
	return rc;
}
