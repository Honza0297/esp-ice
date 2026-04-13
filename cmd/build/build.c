/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/build/build.c
 * @brief "ice build" subcommand.
 *
 * Configures and builds with "cmake --build".
 *
 * Default mode: each output line updates a single progress line
 * with a label prefix. All output is logged to a file (plain text,
 * no ANSI codes). On failure, the last N lines are colorized and
 * shown.
 *
 * Verbose mode (-v): output passes through to the terminal.
 */
#include <time.h>

#include "../../ice.h"

#define TAIL_LINES 30

static int verbose;

static const char *build_usage[] = {
	"ice build [-B <path>] [-v]",
	NULL,
};

static const char *fmt_time(time_t start, struct sbuf *buf)
{
	double elapsed = difftime(time(NULL), start);

	if (elapsed < 1)
		return "<1s";

	sbuf_reset(buf);
	sbuf_addf(buf, "%.0fs", elapsed);
	return buf->buf;
}

/** Keyword → color rules for compiler/build output. */
static const struct color_rule color_rules[] = {
	/* Order matters: longer matches first. */
	COLOR_RULE("fatal error:",          "COLOR_BOLD_RED"),
	COLOR_RULE("undefined reference to", "COLOR_RED"),
	COLOR_RULE("multiple definition of", "COLOR_RED"),
	COLOR_RULE("In file included from",  "COLOR_CYAN"),
	COLOR_RULE("In function",            "COLOR_CYAN"),
	COLOR_RULE("CMake Error",            "COLOR_RED"),
	COLOR_RULE("CMake Warning",          "COLOR_YELLOW"),
	COLOR_RULE("FAILED:",                "COLOR_BOLD_RED"),
	COLOR_RULE("warning:",               "COLOR_BOLD_YELLOW"),
	COLOR_RULE("error:",                 "COLOR_BOLD_RED"),
	COLOR_RULE("note:",                  "COLOR_CYAN"),
	COLOR_RULE("***",                    "COLOR_RED"),
	{ NULL },
};

/**
 * @brief Show the last TAIL_LINES non-progress lines from the log,
 * colorized for terminal display.
 */
static void show_tail(const char *logpath)
{
	struct sbuf log = SBUF_INIT;
	struct sbuf filtered = SBUF_INIT;
	int count = 0;

	if (sbuf_read_file(&log, logpath) < 0) {
		sbuf_release(&log);
		return;
	}

	/* Collect non-progress lines. */
	const char *p = log.buf;
	const char *end = log.buf + log.len;

	while (p < end) {
		const char *nl = memchr(p, '\n', end - p);
		if (!nl)
			nl = end;

		/* Skip progress lines (ninja [n/total], make [nn%],
		 * cmake --) */
		if (*p != '[' && *p != '-') {
			struct sbuf colored = SBUF_INIT;
			color_text(&colored, p, nl - p, color_rules);
			sbuf_addstr(&filtered, colored.buf);
			sbuf_addch(&filtered, '\n');
			sbuf_release(&colored);
		}

		p = nl + 1;
	}

	/* Take last TAIL_LINES. */
	p = filtered.buf + filtered.len;
	while (p > filtered.buf && count < TAIL_LINES) {
		p--;
		if (*p == '\n')
			count++;
	}
	if (*p == '\n')
		p++;

	fprintf(stderr, "\n");
	/* Use fputs so @x{...} tokens get expanded. */
	fputs(p, stderr);

	sbuf_release(&filtered);
	sbuf_release(&log);
}

/**
 * @brief Run a command with labeled progress and logging.
 */
static int run_with_log(const char **argv, const char *label,
			const char *done_label, const char *fail_label,
			const char *logpath)
{
	struct process proc = PROCESS_INIT;
	struct sbuf partial = SBUF_INIT;
	struct sbuf tsb = SBUF_INIT;
	FILE *logfp;
	time_t start;
	char buf[8192];
	ssize_t n;
	int rc;
	int width;
	int prefix_len;

	logfp = fopen(logpath, "w");
	if (!logfp)
		die_errno("cannot create '%s'", logpath);

	prefix_len = strlen(label) + 2; /* "label: " */
	start = time(NULL);
	proc.argv = argv;
	proc.pipe_out = 1;
	proc.merge_err = 1;

	if (process_start(&proc)) {
		fclose(logfp);
		return -1;
	}

	while ((n = read(proc.out, buf, sizeof(buf))) > 0) {
		fwrite(buf, 1, n, logfp);

		const char *p = buf;
		const char *end = buf + n;

		while (p < end) {
			const char *nl = memchr(p, '\n', end - p);

			if (!nl) {
				sbuf_add(&partial, p, end - p);
				break;
			}

			const char *line = p;
			size_t len = nl - p;

			if (partial.len > 0) {
				sbuf_add(&partial, p, nl - p);
				line = partial.buf;
				len = partial.len;
			}

			width = term_width(STDERR_FILENO);
			int avail = width - 3 - prefix_len - 4;

			fprintf(stderr, "\r\033[K   @b{%s}: ", label);
			if (avail > 0 && (int)len > avail) {
				fwrite(line, 1, avail, stderr);
				fprintf(stderr, "...");
			} else {
				fwrite(line, 1, len, stderr);
			}

			sbuf_reset(&partial);
			p = nl + 1;
		}
	}

	rc = process_finish(&proc);
	fclose(logfp);

	fprintf(stderr, "\r\033[K");
	if (rc == 0) {
		fprintf(stderr, " @g{*} @b{%s} (%s)\n",
			done_label, fmt_time(start, &tsb));
	} else {
		show_tail(logpath);
		fprintf(stderr,
			"\n @r{*} @b{%s (%s) -- last %d lines above, full log: %s}\n",
			fail_label, fmt_time(start, &tsb),
			TAIL_LINES, logpath);
	}

	sbuf_release(&tsb);
	sbuf_release(&partial);
	return rc;
}

int cmd_build(int argc, const char **argv)
{
	const char *build_dir = "build";
	struct sbuf path = SBUF_INIT;
	struct sbuf logpath = SBUF_INIT;
	int rc;

	struct option opts[] = {
		OPT_STRING('B', "build-dir", &build_dir, "path",
			   "build directory"),
		OPT_BOOL('v', "verbose", &verbose,
			 "show full build output"),
		OPT_END(),
	};

	parse_options(argc, argv, opts, build_usage);

	if (access("CMakeLists.txt", F_OK) != 0)
		die("no CMakeLists.txt found in current directory");

	/* Ensure log directory exists. */
	sbuf_addf(&logpath, "%s/log", build_dir);
	mkdir(build_dir, 0755);
	mkdir(logpath.buf, 0755);
	sbuf_reset(&logpath);

	/* Configure if not yet configured. */
	sbuf_addf(&path, "%s/CMakeCache.txt", build_dir);
	if (access(path.buf, F_OK) != 0) {
		const char *cmake_argv[] = {
			"cmake", "-G", "Ninja", "-B", build_dir, NULL
		};

		sbuf_addf(&logpath, "%s/log/configure.log", build_dir);

		if (verbose) {
			struct process proc = PROCESS_INIT;
			proc.argv = cmake_argv;
			rc = process_run(&proc);
		} else {
			rc = run_with_log(cmake_argv, "Configuring",
					  "Configured", "Configure failed",
					  logpath.buf);
		}

		if (rc) {
			sbuf_release(&path);
			sbuf_release(&logpath);
			return rc;
		}
	}
	sbuf_release(&path);

	/* Build. */
	const char *build_argv[] = {
		"cmake", "--build", build_dir, NULL
	};

	sbuf_reset(&logpath);
	sbuf_addf(&logpath, "%s/log/build.log", build_dir);

	if (verbose) {
		struct process proc = PROCESS_INIT;
		proc.argv = build_argv;
		rc = process_run(&proc);
	} else {
		rc = run_with_log(build_argv, "Building", "Built",
				  "Build failed", logpath.buf);
	}

	sbuf_release(&logpath);
	return rc;
}
