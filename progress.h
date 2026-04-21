/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file progress.h
 * @brief Run a child process with a single-line progress indicator.
 *
 * Orchestrator commands (build, flash, init, ...) spawn tools whose
 * output is noisy but only interesting on failure.  process_run_progress
 * captures the full stdout/stderr to a log file under
 * ~/.ice/logs -- always -- and either draws a spinner line (quiet
 * mode) or streams the output live (verbose).  On failure the log
 * is re-read and printed (colorized) so the user sees what went
 * wrong without having to open the log file.  Callers can pass a
 * filter callback to trim the dump to relevant lines (e.g. ninja's
 * FAILED: blocks for @b{ice build}).
 */
#ifndef PROGRESS_H
#define PROGRESS_H

#include <stddef.h>

struct process;
struct sbuf;

/**
 * @brief Filter the captured log into the text to print on failure.
 *
 * Called once when the child exits non-zero (and only in non-verbose
 * mode -- in verbose the user has already seen the output live).  The
 * callback receives the full captured log and must append the text it
 * wants displayed to @p out as plain text; process_run_progress
 * colorizes the result with a default rule set before printing.
 *
 * Leaving @p out empty suppresses the dump (only the trailing hint
 * pointing at the log file is emitted).  NULL on_fail defaults to an
 * identity filter: the whole log is printed.
 *
 * @param out  Destination buffer; append filtered plain text.
 * @param log  Full captured log (NUL-terminated).
 * @param len  Length of @p log in bytes.
 */
typedef void (*progress_fail_cb)(struct sbuf *out, const char *log, size_t len);

/**
 * @brief Run a child process, teeing its merged output to a log file.
 *
 * Forces @c pipe_out and @c merge_err on @p proc, starts it, and
 * pumps the combined stdout+stderr into a per-invocation log file at
 * @c ~/.ice/logs/YYYYMMDD-HHMMSS-<slug>.log -- the directory tree is
 * created on demand.
 *
 * In quiet mode (@c global_verbose == 0) a single status line
 * "<spinner> @p msg (<elapsed>)" is drawn on stdout; on completion
 * the line is replaced with "@c{✓} @p msg done. (<elapsed>)" on
 * success or "@c{✗} @p msg failed. (<elapsed>)" on failure.  In
 * verbose mode the captured bytes are also written to stdout in real
 * time and no spinner is drawn, but the final status line is still
 * emitted and the log file is still produced identically.
 *
 * On non-zero exit codes (and in non-verbose mode) the captured log
 * is re-read, passed through @p on_fail (or dumped whole if NULL),
 * colorized and written to stderr.  A trailing @ref hint() pointing
 * at the full log path is always emitted.
 *
 * The caller must set @c proc->argv (and may set @c dir, @c env);
 * the pipe_* / merge_err flags are set internally and will be
 * overwritten.
 *
 * @param proc     Process descriptor (argv must be set).
 * @param msg      Progress line text, e.g. "Building".
 * @param slug     Short identifier embedded in the log filename, e.g.
 *                 "build", "flash", "init-cmake".  Must be safe as a
 *                 path component.
 * @param on_fail  Optional filter for the failure dump; NULL means
 *                 print the whole log.
 * @return The child's exit code, or -1 on spawn or I/O failure.
 */
int process_run_progress(struct process *proc, const char *msg,
			 const char *slug, progress_fail_cb on_fail);

#endif /* PROGRESS_H */
