/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmake.h
 * @brief Shared cmake orchestration for "ice" commands.
 *
 * Every cmake-based "ice" command calls one of the two primitives
 * below.  Each reads core.build-dir / core.generator / core.verbose
 * / cmake.define from the config store, so callers don't thread the
 * same settings through.  Logs land under <build-dir>/log/.
 */
#ifndef CMAKE_H
#define CMAKE_H

/**
 * Configure the build directory.
 *
 * Runs cmake when CMakeCache.txt is missing, when a cmake.define
 * differs from the cached value, or when @p force is non-zero.
 *
 * @return 0 on success, non-zero on failure.
 */
int ensure_build_directory(int force);

/**
 * Ensure the build directory is configured, then invoke @p target.
 *
 * @p label is shown in the progress display as "Running <label>:",
 * and on completion as "<label>" (success) or "<label> failed"
 * (failure).
 *
 * @p interactive non-zero runs the target with stdio connected to
 * the terminal (no capture, no progress display) -- required for
 * ncurses TUI targets like menuconfig.
 *
 * @return 0 on success, non-zero on failure.
 */
int run_cmake_target(const char *target, const char *label, int interactive);

#endif /* CMAKE_H */
