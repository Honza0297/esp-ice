/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/idf/component/solve.h
 * @brief Resolve manifest deps to concrete versions via PubGrub.
 *
 * Given a project's root dependencies (collected from each local
 * component's @c idf_component.yml), the solver:
 *
 *   1. Synthesises an @c idf "package" from @p idf_version so
 *      @c source: idf constraints can be satisfied without consulting
 *      the registry.
 *   2. BFS-walks the registry: for each unseen @c (name, registry_url)
 *      pair, fetches the component's metadata (see @c registry.h) and
 *      registers every version + its declared deps as PubGrub facts.
 *   3. Runs the solver.
 *   4. For each chosen version, returns a @c solve_resolved entry that
 *      carries everything @c lockfile_save() and @c fetch_component()
 *      need (registry URL, download URL, content hash, transitive
 *      dependencies).
 *
 * @c if / @c rules conditional dependencies are not yet evaluated --
 * they are registered unconditionally.  This may over-fetch but keeps
 * the PoC small; @c rules.c will be wired in once the simple path is
 * proven on real projects.
 */
#ifndef CMD_IDF_COMPONENT_SOLVE_H
#define CMD_IDF_COMPONENT_SOLVE_H

#include <stddef.h>

#include "cmd/idf/component/lockfile.h"

struct sbuf;

/** A single root-level dependency. */
struct solve_root {
	const char *name;	  /**< "namespace/name" or "idf"; required. */
	const char *spec;	  /**< Constraint string; required. */
	int is_idf;		  /**< 1 for IDF deps. */
	const char *registry_url; /**< Default registry for service roots. */
};

/** One resolved component the caller should materialise. */
struct solve_resolved {
	char *name;	      /**< "namespace/name" or "idf". */
	char *version;	      /**< Chosen version string. */
	int is_idf;	      /**< 1 if this is the synthesised idf entry. */
	char *registry_url;   /**< Service entries only; NULL otherwise. */
	char *download_url;   /**< Service entries only; absolute URL. */
	char *component_hash; /**< Service entries only; hex digest. */
	struct lockfile_nested_dep
	    *nested; /**< Transitive deps (lockfile shape). */
	size_t nested_nr;
};

/**
 * @brief Solve @p roots to a concrete set of versions.
 *
 * On success, fills @p *out / @p *out_nr; release each entry with
 * @c solve_resolved_release() and free the array.  On failure, writes
 * a human-readable explanation to @p err (caller-owned sbuf) and
 * returns -1.
 *
 * @return 0 on success, -1 if no satisfying assignment exists.
 */
int solve_resolve(const struct solve_root *roots, size_t roots_nr,
		  const char *idf_version, const char *default_registry_url,
		  struct solve_resolved **out, size_t *out_nr,
		  struct sbuf *err);

/** Release fields owned by @p r; does not free @p r itself. */
void solve_resolved_release(struct solve_resolved *r);

#endif /* CMD_IDF_COMPONENT_SOLVE_H */
