/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/idf/component/fetch.h
 * @brief Primitives for materialising managed components.
 *
 * Phase 4a: the building blocks (name normalisation, sha256 digest,
 * HTTP download).  The end-to-end @c fetch_component() that takes a
 * lockfile entry and produces a populated @c managed_components/<name>/
 * directory depends on a ZIP extractor (see @c zip.h, forthcoming) and
 * a git clone helper; it lands in subsequent phases.
 */
#ifndef CMD_IDF_COMPONENT_FETCH_H
#define CMD_IDF_COMPONENT_FETCH_H

#include <stddef.h>

struct sbuf;
struct lockfile_entry;

/**
 * @brief Materialise one resolved component into @p managed_components_dir.
 *
 * Dispatches on @c entry->src_type:
 *
 *   - @c LOCKFILE_SRC_IDF / @c LOCKFILE_SRC_LOCAL -- no-op, the
 *     component is already on disk.
 *   - @c LOCKFILE_SRC_SERVICE -- discover the registry's storage URL
 *     via @c <registry>/api/, fetch the component metadata JSON,
 *     match the resolved version, download the zip archive, and
 *     extract into @c managed_components_dir/<build_name>/.
 *   - @c LOCKFILE_SRC_GIT -- clone @c entry->git_url into the same
 *     target, then check out @c entry->git_ref.
 *
 * The target directory is removed first if it exists, so the
 * operation is idempotent.  @p managed_components_dir is created
 * if missing.
 *
 * @return 0 on success, -1 on any error (network, parse, I/O).
 */
int fetch_component(const struct lockfile_entry *entry,
		    const char *managed_components_dir);

/**
 * @brief On-disk directory name for a registry component.
 *
 * Appends @p name with @c "/" replaced by @c "__" to @p out, matching
 * Python @c idf_component_manager's @c build_name.  Does not clear
 * @p out first.  Examples:
 *
 *   "example/cmp"        -> "example__cmp"
 *   "espressif/cjson"    -> "espressif__cjson"
 *   "idf"                -> "idf"
 */
void fetch_build_name(struct sbuf *out, const char *name);

/**
 * @brief Compute the lowercase hex sha256 digest of @p path.
 *
 * Writes 64 hex chars plus NUL into @p out_hex (min. 65 bytes).
 *
 * @return 0 on success, -1 on I/O error.
 */
int fetch_compute_sha256(const char *path, char out_hex[65]);

/**
 * @brief Verify a file's sha256 matches @p expected_hex.
 *
 * @p expected_hex is compared case-insensitively so callers can pass
 * either upper- or lower-case digests.
 *
 * @return 0 on match, -1 on mismatch or I/O error.
 */
int fetch_verify_sha256(const char *path, const char *expected_hex);

/**
 * @brief Download @p url into @p dest_path.
 *
 * Thin wrapper over @c http_download() that creates @p dest_path's
 * parent directory tree if needed and downloads silently (no
 * progress bar).  Partial files are cleaned up on failure.
 *
 * @return 0 on success, -1 on error.
 */
int fetch_download(const char *url, const char *dest_path);

#endif /* CMD_IDF_COMPONENT_FETCH_H */
