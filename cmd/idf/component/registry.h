/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/idf/component/registry.h
 * @brief ESP Component Registry metadata client.
 *
 * Two-step lookup, matching the registry's documented surface:
 *
 *   1. @c GET <registry_url>/api/ -> @c components_base_url (storage URL).
 *   2. @c GET <storage>/components/<full_name>.json -> per-version
 *      metadata (download URL, component_hash, declared dependencies).
 *
 * Each version's @c dependencies are returned in a stable shape that
 * the solver can feed to @c pubgrub_solver_add() directly: every entry
 * has a non-NULL @c name and @c spec.  IDF deps (@c source: idf with
 * a null name in the registry response) are normalised to
 * @c name = "idf", @c is_idf = 1 so callers don't carry that
 * special-case downstream.
 *
 * Network errors die() rather than returning an error code -- the
 * solver has nothing useful to do without the metadata, and the build
 * has nothing useful to do without the solver.
 */
#ifndef CMD_IDF_COMPONENT_REGISTRY_H
#define CMD_IDF_COMPONENT_REGISTRY_H

#include <stddef.h>

/** A single declared dependency of a registry version. */
struct reg_dep {
	char *name;	    /**< "namespace/name" or "idf"; never NULL. */
	char *spec;	    /**< Constraint, e.g. ">=5.0"; never NULL. */
	int is_idf;	    /**< 1 if @c source: idf in the response. */
	char *registry_url; /**< Base URL for service deps; NULL for IDF. */
	char *if_expr;	    /**< Top-level @c if expression; NULL if absent. */
	char **rules_if;    /**< Per-rule @c if expressions. */
	size_t rules_nr;
};

/** One published version of a component. */
struct reg_version {
	char *version;	    /**< Resolved version, e.g. "3.0.0". */
	char *download_url; /**< Absolute URL of the zip archive. */
	char
	    *component_hash; /**< Hex sha256 of the archive (NULL if absent). */
	struct reg_dep *deps;
	size_t deps_nr;
};

/** Cached metadata for one component. */
struct reg_component {
	char *name;	    /**< "namespace/name" the caller asked for. */
	char *registry_url; /**< Base URL the response came from. */
	struct reg_version *versions;
	size_t versions_nr;
};

/** Static initializer. */
#define REG_COMPONENT_INIT {0}

/**
 * @brief Discover the storage URL for @p registry_url.
 *
 * Caches an in-process map of @c registry_url -> storage URL so
 * repeated calls don't re-hit @c /api/.  The returned pointer stays
 * valid for the lifetime of the process; callers must not free it.
 *
 * @return Storage URL with a trailing slash, or NULL on error.
 */
const char *reg_storage_url(const char *registry_url);

/**
 * @brief Fetch and parse the metadata JSON for @p full_name.
 *
 * On success fills @p out with freshly allocated state; release via
 * @c reg_release().  On HTTP/parse error returns -1 and @p out is
 * untouched.
 *
 * @return 0 on success, -1 on error.
 */
int reg_fetch_component(struct reg_component *out, const char *registry_url,
			const char *full_name);

/** Release everything @p c owns; leaves it in the init state. */
void reg_release(struct reg_component *c);

#endif /* CMD_IDF_COMPONENT_REGISTRY_H */
