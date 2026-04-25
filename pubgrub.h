/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file pubgrub.h
 * @brief PubGrub version-constraint solver over @c semver.h.
 *
 * Implements Natalie Weizenbaum's PubGrub algorithm (Dart pub, 2018)
 * for SemVer packages.  Given a set of
 * @c (package, version, dependencies) facts plus a set of root
 * dependencies, the solver picks one version per required package
 * satisfying every accumulated constraint, or reports that no such
 * assignment exists.
 *
 * Lifecycle follows the value-type idiom used by @c sbuf:
 *
 *   struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
 *   struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;
 *
 *   pubgrub_solver_root_dep(&s, "a", "*");
 *   pubgrub_solver_root_dep(&s, "b", "*");
 *
 *   struct pubgrub_dep deps[] = { {"c", "^1.0.0"} };
 *   pubgrub_solver_add(&s, "a", "2.0.0", deps, 1);
 *   pubgrub_solver_add(&s, "a", "1.0.0", NULL, 0);
 *   ...
 *
 *   if (pubgrub_solver_solve(&s, &sol) == 0) {
 *           for (size_t i = 0; i < pubgrub_solution_count(&sol); i++)
 *                   printf("%s = %s\n",
 *                          pubgrub_solution_name(&sol, i),
 *                          pubgrub_solution_version(&sol, i));
 *   } else {
 *           fputs(pubgrub_solver_error(&s), stderr);
 *   }
 *
 *   pubgrub_solution_release(&sol);
 *   pubgrub_solver_release(&s);
 *
 * Version-selection policy: for each package the highest version
 * satisfying the accumulated constraints is tried first; the solver
 * backtracks using PubGrub-style conflict-driven backjumping when a
 * choice introduces a contradiction.  Given the same inputs the
 * solver is deterministic -- registration order breaks ties in the
 * search.
 *
 * Strings passed into the solver are copied internally; callers keep
 * ownership of their buffers.
 */
#ifndef PUBGRUB_H
#define PUBGRUB_H

#include <stddef.h>

/* Opaque implementation state.  Defined in pubgrub.c.  Value-type
 * wrappers below only expose a single pointer field, mirroring sbuf's
 * caller-allocates-the-header / impl-owns-the-guts shape. */
struct pubgrub_solver_state;
struct pubgrub_solution_state;

/* Forward declarations so signatures can mention both types before
 * either is fully defined below. */
struct pubgrub_solver;
struct pubgrub_solution;

/* ------------------------------------------------------------------ */
/* Dependency input                                                    */
/* ------------------------------------------------------------------ */

/** A single dependency: package @c name constrained by @c spec. */
struct pubgrub_dep {
	const char *name; /**< Dependency's package name. */
	const char *spec; /**< SemVer constraint (see @c semver.h). */
};

/* ------------------------------------------------------------------ */
/* Solver                                                              */
/* ------------------------------------------------------------------ */

struct pubgrub_solver {
	struct pubgrub_solver_state *state; /**< Opaque; do not touch. */
};

/** Static initializer for an empty solver. */
#define PUBGRUB_SOLVER_INIT {NULL}

/** Initialize @p s in place (equivalent to PUBGRUB_SOLVER_INIT). */
void pubgrub_solver_init(struct pubgrub_solver *s);

/** Release everything @p s owns.  Leaves @p s in the init state, so
 * it may be reused or released again without harm. */
void pubgrub_solver_release(struct pubgrub_solver *s);

/**
 * @brief Declare a root-level dependency.
 *
 * Equivalent to a dependency from the project's own manifest.
 * Repeated calls with the same @p name accumulate into a conjunction
 * of constraints on that package.
 *
 * @return 0 on success, -1 if @p spec is malformed.
 */
int pubgrub_solver_root_dep(struct pubgrub_solver *s, const char *name,
			    const char *spec);

/**
 * @brief Register a concrete package version and its dependencies.
 *
 * @param deps   array of @p ndeps dependencies; may be NULL if
 *               @p ndeps is zero.  The array and its strings only
 *               need to live for the duration of this call.
 *
 * @return 0 on success, -1 if @p version or any @p deps spec is
 *         malformed, or if @p (name, version) was already registered.
 */
int pubgrub_solver_add(struct pubgrub_solver *s, const char *name,
		       const char *version, const struct pubgrub_dep *deps,
		       size_t ndeps);

/**
 * @brief Run the solver, filling @p out with a solution on success.
 *
 * @p out may be in the init state or hold a prior solution (which is
 * released first).  On failure @p out is left in the init state and
 * @c pubgrub_solver_error() returns a human-readable explanation.
 *
 * @return 0 if a solution was found, -1 otherwise.
 */
int pubgrub_solver_solve(struct pubgrub_solver *s,
			 struct pubgrub_solution *out);

/**
 * @brief Last error message on @p s, or an empty string.
 *
 * Owned by @p s; valid until the next mutating call on the solver.
 */
const char *pubgrub_solver_error(const struct pubgrub_solver *s);

/* ------------------------------------------------------------------ */
/* Solution                                                            */
/* ------------------------------------------------------------------ */

struct pubgrub_solution {
	struct pubgrub_solution_state *state; /**< Opaque; do not touch. */
};

/** Static initializer for an empty solution. */
#define PUBGRUB_SOLUTION_INIT {NULL}

/** Initialize @p sol in place (equivalent to PUBGRUB_SOLUTION_INIT). */
void pubgrub_solution_init(struct pubgrub_solution *sol);

/** Release everything @p sol owns; leaves it in the init state. */
void pubgrub_solution_release(struct pubgrub_solution *sol);

/** Number of decided packages in @p sol (excluding the implicit root). */
size_t pubgrub_solution_count(const struct pubgrub_solution *sol);

/** Package name at index @p i (stable for the life of the solution). */
const char *pubgrub_solution_name(const struct pubgrub_solution *sol, size_t i);

/** Chosen version string at index @p i. */
const char *pubgrub_solution_version(const struct pubgrub_solution *sol,
				     size_t i);

/**
 * @brief Look up the chosen version for @p name.
 *
 * @return Version string, or NULL if @p name is not in the solution.
 *         Ownership stays with @p sol.
 */
const char *pubgrub_solution_version_of(const struct pubgrub_solution *sol,
					const char *name);

#endif /* PUBGRUB_H */
