/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/idf/component/lockfile.h
 * @brief dependencies.lock (v3.0.0) parser and writer.
 *
 * The solver produces one; @c ice idf component inject reads it back.
 * Actual on-disk shape -- captured from running the Python tool on
 * @c examples/build_system/cmake/component_manager:
 *
 *   version: "3.0.0"
 *   target: esp32
 *   manifest_hash: "139a0e908bf372b..."
 *   direct_dependencies:
 *     - example/cmp
 *     - idf
 *   dependencies:
 *     example/cmp:
 *       version: "3.3.9~1"
 *       component_hash: "c29f41d59ec860..."
 *       source:
 *         type: service
 *         registry_url: "https://components.espressif.com/"
 *       dependencies:
 *         - name: idf
 *           require: private
 *           version: ">=4.1"
 *     idf:
 *       version: "6.1.0"
 *       source:
 *         type: idf
 *
 * Key differences from a naive "list of entries" reading:
 *   - @c dependencies at the top level is a MAP keyed by component
 *     name, not a sequence.  Entries are sorted alphabetically on
 *     write.
 *   - Source @c type values are @c service / @c idf / @c git / @c local
 *     (note @c service, not @c web_service).
 *   - Registry URL lives under @c source.registry_url.
 *   - Content digest is @c component_hash (hex), not @c sha256.
 *   - Each entry carries its own @c dependencies sub-list recording
 *     the transitive deps the solver visited.
 *
 * Unknown @c type values parse as @c LOCKFILE_SRC_UNKNOWN so the
 * caller can detect new source kinds without the parser bailing.
 */
#ifndef CMD_IDF_COMPONENT_LOCKFILE_H
#define CMD_IDF_COMPONENT_LOCKFILE_H

#include <stddef.h>

enum lockfile_source_type {
	LOCKFILE_SRC_UNKNOWN,
	LOCKFILE_SRC_IDF,
	LOCKFILE_SRC_SERVICE, /**< ESP Component Registry */
	LOCKFILE_SRC_GIT,
	LOCKFILE_SRC_LOCAL,
};

/** One transitive dep recorded under an entry's @c dependencies list. */
struct lockfile_nested_dep {
	char *name;
	char *version; /**< spec string like ">=4.1" */
	char *require; /**< "public" / "private" / NULL */
};

struct lockfile_entry {
	char *name;    /**< key in the dependencies map */
	char *version; /**< resolved version string */
	enum lockfile_source_type src_type;

	/* Source-specific fields.  Unused ones stay NULL. */
	char *registry_url; /**< service */
	char *git_url;	    /**< git */
	char *git_ref;	    /**< git */
	char *path;	    /**< local */

	char *component_hash; /**< service only, hex digest */

	struct lockfile_nested_dep *nested; /**< entry's own dependencies */
	size_t nested_nr;
};

struct lockfile {
	char *lock_version;	    /**< "3.0.0"; NULL if absent */
	char *target;		    /**< chip name */
	char *manifest_hash;	    /**< hex digest of the root manifest */
	char **direct_dependencies; /**< names of root's direct deps */
	size_t direct_dependencies_nr;
	struct lockfile_entry *entries;
	size_t nr;
};

/** Static initializer. */
#define LOCKFILE_INIT {0}

/**
 * @brief Parse a lockfile from a YAML buffer.
 *
 * On success fills @p out with freshly allocated state; release via
 * @c lockfile_release().  On failure @p out is untouched.
 *
 * @return 0 on success, -1 on parse error.
 */
int lockfile_parse(struct lockfile *out, const char *text, size_t len);

/** Load from a file. */
int lockfile_load(struct lockfile *out, const char *path);

/**
 * @brief Write @p lf to @p path.
 *
 * Emits a readable, deterministic YAML dialect (keys alphabetical,
 * entries alphabetical).  Not a byte-identical match for the Python
 * tool's ruamel.yaml output -- consider that a polish task if/when
 * round-tripping with Python in the same repo becomes important.
 *
 * @return 0 on success, -1 on I/O failure.
 */
int lockfile_save(const struct lockfile *lf, const char *path);

/** Release owned memory; leaves @p lf in the init state. */
void lockfile_release(struct lockfile *lf);

#endif /* CMD_IDF_COMPONENT_LOCKFILE_H */
