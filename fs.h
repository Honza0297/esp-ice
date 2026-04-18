/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file fs.h
 * @brief Portable filesystem helpers built on the platform primitives.
 *
 * These sit at the project root because the recursion / path walking
 * is ordinary C on top of mkdir(), is_directory(), dir_foreach(),
 * unlink(), rmdir() — all of which platform.h already abstracts.
 */
#ifndef FS_H
#define FS_H

#include <stddef.h>

/**
 * @brief Create a directory and any missing intermediate parents.
 *
 * Equivalent to `mkdir -p`.  Treats EEXIST on an existing component
 * as success.  Accepts both '/' and '\\' as separators.  On Windows,
 * a leading "X:" drive prefix is skipped so mkdir() is not called on
 * a drive root.  Trailing separators are tolerated.
 *
 * @param dir  Directory path to create (UTF-8 on Windows).
 * @return 0 on success, -1 on the first non-EEXIST mkdir failure
 *         (errno is that of the failing mkdir).
 */
int mkdirp(const char *dir);

/**
 * @brief Create the parent directory tree of a file path.
 *
 * Strips the final path component (the filename) and calls mkdirp()
 * on the remainder.  A no-op if @p path has no directory separator.
 *
 * @param path  File path whose parents to create (UTF-8 on Windows).
 * @return 0 on success, -1 on mkdir failure.
 */
int mkdirp_for_file(const char *path);

/**
 * @brief Write @p len bytes of @p data to @p path atomically.
 *
 * The content goes to `@p path.tmp` first and is renamed onto @p path
 * on success.  `rename()` is atomic on POSIX and atomic-replace on
 * Windows (via rename_w / MoveFileExW), so a crash mid-write leaves
 * the original @p path untouched -- no truncate-then-partial-write
 * window.
 *
 * On failure the tmp file is removed; errno reflects the failing call.
 *
 * @return 0 on success, -1 on any I/O failure.
 */
int write_file_atomic(const char *path, const void *data, size_t len);

/**
 * @brief Recursively delete the contents of @p path.
 *
 * The directory @p path itself is not removed — the caller decides
 * whether to rmdir() it afterwards.  Errors on individual entries are
 * reported via warn_errno() and cause the overall return to be -1,
 * but iteration continues so a partial cleanup still makes progress.
 *
 * @param path     Directory whose contents to remove.
 * @param verbose  If non-zero, print every removed entry to stdout.
 * @return 0 on full success, -1 if any entry could not be removed or
 *         @p path could not be opened.
 */
int rmtree(const char *path, int verbose);

/**
 * @brief Check whether @p name is an executable on PATH.
 * @return 1 if found and executable, 0 otherwise.
 */
int find_in_path(const char *name);

/**
 * @brief Recursively hardlink the contents of @p src into @p dst.
 *
 * @p dst is created if missing; existing files inside @p dst cause
 * link() to fail EEXIST and abort the copy.  Directories are
 * recreated in @p dst and their contents processed recursively;
 * regular files are hardlinked via link(); anything else (symlinks,
 * sockets, devices) errors out.
 *
 * @p src and @p dst must live on the same filesystem -- link() fails
 * EXDEV across mounts.
 *
 * Intended for append-only content like git's object store: when a
 * consumer later rewrites a file via atomic rename, the rename
 * creates a new inode so the modification doesn't propagate to
 * @p src; but any in-place edit to a shared inode DOES propagate,
 * which callers must account for.
 *
 * @return 0 on success, -1 if any directory scan or link() failed.
 */
int hardlink_tree(const char *src, const char *dst);

#endif /* FS_H */
