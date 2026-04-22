/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file hints.h
 * @brief Match YAML-defined regex rules against a log file.
 *
 * Reproduces the semantics of ESP-IDF's
 * @c tools/idf_py_actions/tools.py::generate_hints_buffer as a library
 * function so both the @c ice @c idf @c hints CLI and the
 * @c process_run_progress failure path can invoke it.
 *
 * The rules file is a YAML sequence of @c {re, hint, match_to_output,
 * variables} mappings; see the ESP-IDF hints.yml for the schema.  The
 * log is normalized by stripping each line's leading and trailing
 * whitespace, dropping empty lines, and joining the remainder with
 * single spaces before any regex runs.  Matching uses PCRE2, so Perl
 * extensions (@c \w, @c \d, negative lookahead) are honored.
 *
 * Matching rules print @b{HINT:} lines to stdout (yellow when stdout
 * is a tty).  Broken individual rules @c warn() and are skipped so one
 * typo in hints.yml can't suppress every other match.
 */
#ifndef HINTS_H
#define HINTS_H

/**
 * @brief Scan a log file against rules from a hints YAML file.
 *
 * @param hints_yml_path  Path to a YAML rules file.
 * @param log_path        Path to the log to scan.
 * @return Number of HINT lines printed, or -1 if either file can't be
 *         read or @p hints_yml_path is not a valid rules document.
 */
int hints_scan(const char *hints_yml_path, const char *log_path);

#endif /* HINTS_H */
