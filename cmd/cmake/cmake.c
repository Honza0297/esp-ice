/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/cmake/cmake.c
 * @brief Shared cmake infrastructure and thin wrapper commands.
 *
 * Provides ensure_build_directory() and run_cmake_target() -- the two
 * building blocks every cmake-based "ice" command uses.  Individual
 * commands (build, clean, flash, ...) are small wrappers at the bottom.
 */
#include <time.h>

#include "../../ice.h"
#include "cmake.h"

#define TAIL_LINES 30

/* ------------------------------------------------------------------ */
/*  Static helpers (progress display, logging, colorization)          */
/* ------------------------------------------------------------------ */

static const char *fmt_time(time_t start, struct sbuf *buf)
{
	double elapsed = difftime(time(NULL), start);

	if (elapsed < 1)
		return "<1s";

	sbuf_reset(buf);
	sbuf_addf(buf, "%.0fs", elapsed);
	return buf->buf;
}

/** Keyword -> color rules for compiler/build output. */
static const struct color_rule color_rules[] = {
	COLOR_RULE("fatal error:",           "COLOR_BOLD_RED"),
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
 * Show the last TAIL_LINES non-progress lines from the log,
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

	const char *p = log.buf;
	const char *end = log.buf + log.len;

	while (p < end) {
		const char *nl = memchr(p, '\n', end - p);
		if (!nl)
			nl = end;

		if (*p != '[' && *p != '-') {
			struct sbuf colored = SBUF_INIT;
			color_text(&colored, p, nl - p, color_rules);
			sbuf_addstr(&filtered, colored.buf);
			sbuf_addch(&filtered, '\n');
			sbuf_release(&colored);
		}

		p = nl + 1;
	}

	p = filtered.buf + filtered.len;
	while (p > filtered.buf && count < TAIL_LINES) {
		p--;
		if (*p == '\n')
			count++;
	}
	if (*p == '\n')
		p++;

	fprintf(stderr, "\n");
	fputs(p, stderr);

	sbuf_release(&filtered);
	sbuf_release(&log);
}

/**
 * Run a command with labeled progress display and logging.
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

	prefix_len = strlen(label) + 2;
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

/* ------------------------------------------------------------------ */
/*  CMakeCache helpers                                                */
/* ------------------------------------------------------------------ */

/**
 * Check whether any -D entry is missing from, or differs in, the cache.
 *
 * Each entry in @p defines is "KEY=VALUE" or "KEY:TYPE=VALUE".
 * The optional :TYPE is stripped before lookup.
 */
static int cache_needs_configure(const char *cache_buf,
				 const struct svec *defines)
{
	for (size_t i = 0; i < defines->nr; i++) {
		const char *entry = defines->v[i];
		const char *eq = strchr(entry, '=');
		struct sbuf key = SBUF_INIT;
		const char *val, *cached;
		int differs;

		if (!eq)
			continue;

		/* Key part: strip optional :TYPE suffix. */
		{
			const char *colon = memchr(entry, ':', eq - entry);
			if (colon)
				sbuf_add(&key, entry, colon - entry);
			else
				sbuf_add(&key, entry, eq - entry);
		}

		val = eq + 1;
		cached = cmakecache_lookup(cache_buf, key.buf);

		if (!cached) {
			sbuf_release(&key);
			return 1;
		}

		/* cached points to VALUE\n or VALUE\0 -- compare up to EOL. */
		{
			size_t val_len = strlen(val);
			size_t cached_len;
			const char *nl = strchr(cached, '\n');

			cached_len = nl ? (size_t)(nl - cached) : strlen(cached);
			differs = val_len != cached_len ||
				  memcmp(val, cached, val_len) != 0;
		}

		sbuf_release(&key);
		if (differs)
			return 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Public infrastructure                                             */
/* ------------------------------------------------------------------ */

int ensure_build_directory(const char *build_dir, const char *generator,
			   const struct svec *defines, int verbose, int force)
{
	struct sbuf cache_path = SBUF_INIT;
	struct sbuf cache_buf = SBUF_INIT;
	struct sbuf logpath = SBUF_INIT;
	int has_cache;
	int needs_configure;
	int rc;

	if (access("CMakeLists.txt", F_OK) != 0)
		die("no CMakeLists.txt found in current directory");

	/* Ensure build and log directories exist. */
	{
		struct sbuf logdir = SBUF_INIT;
		sbuf_addf(&logdir, "%s/log", build_dir);
		mkdir(build_dir, 0755);
		mkdir(logdir.buf, 0755);
		sbuf_release(&logdir);
	}

	/* Read CMakeCache.txt if it exists. */
	sbuf_addf(&cache_path, "%s/CMakeCache.txt", build_dir);
	has_cache = access(cache_path.buf, F_OK) == 0;

	if (has_cache)
		sbuf_read_file(&cache_buf, cache_path.buf);

	/* Decide whether cmake needs to run. */
	needs_configure = force || !has_cache ||
		(defines->nr && cache_needs_configure(cache_buf.buf, defines));

	/* Validate generator against existing cache. */
	if (has_cache) {
		const char *cached_gen = cmakecache_lookup(cache_buf.buf,
							   "CMAKE_GENERATOR");
		if (cached_gen) {
			size_t gen_len = strlen(generator);
			const char *nl = strchr(cached_gen, '\n');
			size_t cached_len = nl ? (size_t)(nl - cached_gen)
					       : strlen(cached_gen);

			if (gen_len != cached_len ||
			    memcmp(generator, cached_gen, gen_len) != 0)
				die("build is configured for generator '%.*s' "
				    "not '%s' -- run 'ice reconfigure' or "
				    "remove '%s'",
				    (int)cached_len, cached_gen,
				    generator, build_dir);
		}
	}

	if (!needs_configure) {
		sbuf_release(&cache_path);
		sbuf_release(&cache_buf);
		return 0;
	}

	/* Build cmake argv dynamically (variable number of -D entries). */
	{
		struct svec args = SVEC_INIT;

		svec_push(&args, "cmake");
		svec_push(&args, "-G");
		svec_push(&args, generator);
		svec_push(&args, "-B");
		svec_push(&args, build_dir);

		for (size_t i = 0; i < defines->nr; i++)
			svec_pushf(&args, "-D%s", defines->v[i]);

		sbuf_addf(&logpath, "%s/log/configure.log", build_dir);

		if (verbose) {
			struct process proc = PROCESS_INIT;
			proc.argv = args.v;
			rc = process_run(&proc);
		} else {
			rc = run_with_log(args.v, "Configuring",
					  "Configured", "Configure failed",
					  logpath.buf);
		}

		svec_clear(&args);
	}

	/* On failure, remove CMakeCache.txt to prevent half-valid state. */
	if (rc && has_cache)
		unlink(cache_path.buf);

	sbuf_release(&logpath);
	sbuf_release(&cache_path);
	sbuf_release(&cache_buf);
	return rc;
}

int run_cmake_target(const char *target, const char *build_dir, int verbose)
{
	struct sbuf logpath = SBUF_INIT;
	struct sbuf fail_label = SBUF_INIT;
	const char *label, *done_label;
	int rc;

	const char *argv[] = {
		"cmake", "--build", build_dir, "--target", target, NULL
	};

	/* Friendly labels for the default build target. */
	if (!strcmp(target, "all")) {
		label = "Building";
		done_label = "Built";
		sbuf_addstr(&fail_label, "Build failed");
	} else {
		label = target;
		done_label = target;
		sbuf_addf(&fail_label, "%s failed", target);
	}

	sbuf_addf(&logpath, "%s/log/%s.log", build_dir, target);

	if (verbose) {
		struct process proc = PROCESS_INIT;
		proc.argv = argv;
		rc = process_run(&proc);
	} else {
		rc = run_with_log(argv, label, done_label,
				  fail_label.buf, logpath.buf);
	}

	sbuf_release(&fail_label);
	sbuf_release(&logpath);
	return rc;
}

/* ------------------------------------------------------------------ */
/*  Wrapper commands                                                  */
/* ------------------------------------------------------------------ */

/*
 * Pull build_dir / generator / verbose / cmake.define entries from
 * the config store.  The defines svec is filled by the caller and
 * must be cleared with svec_clear() after use.
 */
static void gather_cmake_config(const char **build_dir, const char **generator,
				struct svec *defines, int *verbose)
{
	struct config_entry **entries;
	int n;

	*build_dir = config_get("core.build-dir");
	*generator = config_get("core.generator");

	*verbose = 0;
	config_get_bool("core.verbose", verbose);

	n = config_get_all("cmake.define", &entries);
	for (int i = 0; i < n; i++)
		svec_push(defines, entries[i]->value);
	free(entries);
}

static int run_cmake_cmd(int argc, const char **argv, const char *usage_line,
			 const char *target, int force)
{
	const char *usage[] = { usage_line, NULL };
	struct option opts[] = { OPT_END() };
	const char *build_dir, *generator;
	struct svec defines = SVEC_INIT;
	int verbose;
	int rc;

	parse_options(argc, argv, opts, usage);
	gather_cmake_config(&build_dir, &generator, &defines, &verbose);

	rc = ensure_build_directory(build_dir, generator, &defines, verbose,
				    force);
	if (!rc && target)
		rc = run_cmake_target(target, build_dir, verbose);

	svec_clear(&defines);
	return rc;
}

int cmd_build(int argc, const char **argv)
{
	return run_cmake_cmd(argc, argv, "ice build", "all", 0);
}

int cmd_reconfigure(int argc, const char **argv)
{
	return run_cmake_cmd(argc, argv, "ice reconfigure", NULL, 1);
}

int cmd_clean(int argc, const char **argv)
{
	return run_cmake_cmd(argc, argv, "ice clean", "clean", 0);
}

int cmd_flash(int argc, const char **argv)
{
	return run_cmake_cmd(argc, argv, "ice flash", "flash", 0);
}

int cmd_menuconfig(int argc, const char **argv)
{
	return run_cmake_cmd(argc, argv, "ice menuconfig", "menuconfig", 0);
}
