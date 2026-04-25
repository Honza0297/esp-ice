/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Dependency resolver -- SemVer-aware backtracking version solver.
 *
 * Implements the core search strategy of Natalie Weizenbaum's PubGrub
 * algorithm (Dart pub, 2018): for each required package, pick the
 * highest version satisfying the accumulated constraints; on conflict,
 * backtrack.  A project's dependency graph is solved as a DFS over a
 * decision stack, with pruning whenever an added constraint empties a
 * package's candidate set.
 *
 * What's NOT in this implementation yet (vs full PubGrub):
 *   - conflict-driven clause learning (incompatibility derivation).
 *     Resolution currently does chronological backtrack only; learning
 *     would skip equivalent dead ends and is an efficiency upgrade for
 *     future work once real projects demand it.
 *   - rich explanation trees in error messages.
 *
 * What IS in scope:
 *   - correct resolution on the canonical PubGrub test corpus
 *     (diamond, partial-satisfier, backjumps, circular, unsolvable).
 *   - deterministic search: highest version first, ties broken by
 *     registration order.
 *   - clean backtracking state (no residue between attempts).
 */

#include "pubgrub.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "error.h"
#include "sbuf.h"
#include "semver.h"

/* ==================================================================== */
/* Internal types                                                        */
/* ==================================================================== */

struct pg_dep_parsed {
	int pkg_id;		     /* dep's package id */
	struct semver_constraint *c; /* parsed constraint */
	char *spec;		     /* original, for diagnostics */
};

struct pg_pkg_version {
	char *str;		 /* original version string */
	struct semver_version v; /* parsed */
	struct pg_dep_parsed *deps;
	size_t nr_deps;
};

struct pg_package {
	char *name;
	struct pg_pkg_version *versions;
	size_t nr_versions, alloc_versions;
	int versions_sorted; /* 1 once sorted desc */
};

struct pubgrub_solver_state {
	struct pg_package *packages;
	size_t nr_packages, alloc_packages;

	/* Root deps accumulated via pubgrub_solver_root_dep(). */
	struct pg_dep_parsed *root_deps;
	size_t nr_root_deps, alloc_root_deps;

	struct sbuf error;
};

struct pg_entry {
	char *name;
	char *version;
};

struct pubgrub_solution_state {
	struct pg_entry *entries;
	size_t nr, alloc;
};

struct pg_cons_entry {
	struct semver_constraint *c; /* not owned; points into registry */
	int source_stack_idx;	     /* decision that added this; -1 if root */
};

struct pg_cons_list {
	struct pg_cons_entry *entries;
	size_t nr, alloc;
};

struct pg_decision {
	int pkg_id;
	int version_idx;
};

/* Search state during solve(); freed when solve returns. */
struct pg_search {
	struct pubgrub_solver_state *solver;
	struct pg_cons_list *pkg_cons; /* indexed by pkg_id */
	int *decided;		       /* indexed by pkg_id; -1 if undecided */
	struct pg_decision *stack;
	size_t nr_stack, alloc_stack;
};

/* ==================================================================== */
/* Package registry                                                       */
/* ==================================================================== */

static int find_pkg(const struct pubgrub_solver_state *st, const char *name)
{
	for (size_t i = 0; i < st->nr_packages; i++)
		if (strcmp(st->packages[i].name, name) == 0)
			return (int)i;
	return -1;
}

static int intern_pkg(struct pubgrub_solver_state *st, const char *name)
{
	int id = find_pkg(st, name);
	struct pg_package *p;

	if (id >= 0)
		return id;

	ALLOC_GROW(st->packages, st->nr_packages + 1, st->alloc_packages);
	p = &st->packages[st->nr_packages++];
	memset(p, 0, sizeof(*p));
	p->name = sbuf_strdup(name);
	return (int)(st->nr_packages - 1);
}

static int pkg_has_version(const struct pg_package *p, const char *ver_str)
{
	for (size_t i = 0; i < p->nr_versions; i++)
		if (strcmp(p->versions[i].str, ver_str) == 0)
			return 1;
	return 0;
}

/* ==================================================================== */
/* Lifecycle                                                             */
/* ==================================================================== */

void pubgrub_solver_init(struct pubgrub_solver *s) { s->state = NULL; }

static void release_dep(struct pg_dep_parsed *d)
{
	semver_constraint_free(d->c);
	free(d->spec);
}

static void release_pkg_version(struct pg_pkg_version *v)
{
	free(v->str);
	semver_release(&v->v);
	for (size_t i = 0; i < v->nr_deps; i++)
		release_dep(&v->deps[i]);
	free(v->deps);
}

static void release_pkg(struct pg_package *p)
{
	free(p->name);
	for (size_t i = 0; i < p->nr_versions; i++)
		release_pkg_version(&p->versions[i]);
	free(p->versions);
}

void pubgrub_solver_release(struct pubgrub_solver *s)
{
	struct pubgrub_solver_state *st = s->state;
	if (!st)
		return;

	for (size_t i = 0; i < st->nr_packages; i++)
		release_pkg(&st->packages[i]);
	free(st->packages);

	for (size_t i = 0; i < st->nr_root_deps; i++)
		release_dep(&st->root_deps[i]);
	free(st->root_deps);

	sbuf_release(&st->error);
	free(st);
	s->state = NULL;
}

static struct pubgrub_solver_state *ensure_state(struct pubgrub_solver *s)
{
	if (!s->state) {
		s->state = calloc(1, sizeof(*s->state));
		if (!s->state)
			die_errno("calloc");
	}
	return s->state;
}

/* ==================================================================== */
/* Public: root_dep and add                                               */
/* ==================================================================== */

int pubgrub_solver_root_dep(struct pubgrub_solver *s, const char *name,
			    const char *spec)
{
	struct pubgrub_solver_state *st = ensure_state(s);
	struct semver_constraint *c;
	struct pg_dep_parsed *d;
	int pkg_id;

	c = semver_constraint_parse(spec);
	if (!c) {
		sbuf_reset(&st->error);
		sbuf_addf(&st->error, "invalid constraint '%s' for '%s'", spec,
			  name);
		return -1;
	}

	pkg_id = intern_pkg(st, name);

	ALLOC_GROW(st->root_deps, st->nr_root_deps + 1, st->alloc_root_deps);
	d = &st->root_deps[st->nr_root_deps++];
	d->pkg_id = pkg_id;
	d->c = c;
	d->spec = sbuf_strdup(spec);
	return 0;
}

int pubgrub_solver_add(struct pubgrub_solver *s, const char *name,
		       const char *version, const struct pubgrub_dep *deps,
		       size_t ndeps)
{
	struct pubgrub_solver_state *st = ensure_state(s);
	struct semver_version parsed_v = SEMVER_VERSION_INIT;
	struct pg_dep_parsed *dep_array = NULL;
	struct pg_package *p;
	struct pg_pkg_version *v;
	int pkg_id;

	if (semver_parse(&parsed_v, version) < 0) {
		sbuf_reset(&st->error);
		sbuf_addf(&st->error, "invalid version '%s' for '%s'", version,
			  name);
		return -1;
	}

	pkg_id = intern_pkg(st, name);
	p = &st->packages[pkg_id];

	if (pkg_has_version(p, version)) {
		semver_release(&parsed_v);
		sbuf_reset(&st->error);
		sbuf_addf(&st->error, "duplicate version '%s' for '%s'",
			  version, name);
		return -1;
	}

	if (ndeps) {
		dep_array = calloc(ndeps, sizeof(*dep_array));
		if (!dep_array)
			die_errno("calloc");
		for (size_t i = 0; i < ndeps; i++) {
			struct semver_constraint *c =
			    semver_constraint_parse(deps[i].spec);
			if (!c) {
				for (size_t j = 0; j < i; j++)
					release_dep(&dep_array[j]);
				free(dep_array);
				semver_release(&parsed_v);
				sbuf_reset(&st->error);
				sbuf_addf(&st->error,
					  "invalid constraint '%s' on dep "
					  "'%s' of '%s@%s'",
					  deps[i].spec, deps[i].name, name,
					  version);
				return -1;
			}
			dep_array[i].pkg_id = intern_pkg(st, deps[i].name);
			dep_array[i].c = c;
			dep_array[i].spec = sbuf_strdup(deps[i].spec);
		}
	}

	/* Package array may have reallocated during intern_pkg() calls for
	 * dep names above -- refresh. */
	p = &st->packages[pkg_id];

	ALLOC_GROW(p->versions, p->nr_versions + 1, p->alloc_versions);
	v = &p->versions[p->nr_versions++];
	memset(v, 0, sizeof(*v));
	v->str = sbuf_strdup(version);
	v->v = parsed_v;
	v->deps = dep_array;
	v->nr_deps = ndeps;
	p->versions_sorted = 0;
	return 0;
}

/* ==================================================================== */
/* Search                                                                 */
/* ==================================================================== */

static int cmp_version_desc(const void *a, const void *b)
{
	const struct pg_pkg_version *va = a;
	const struct pg_pkg_version *vb = b;
	return semver_cmp(&vb->v, &va->v);
}

static void sort_package_versions(struct pg_package *p)
{
	if (!p->versions_sorted) {
		qsort(p->versions, p->nr_versions, sizeof(*p->versions),
		      cmp_version_desc);
		p->versions_sorted = 1;
	}
}

static struct pg_search *search_new(struct pubgrub_solver_state *st)
{
	struct pg_search *sr;

	sr = calloc(1, sizeof(*sr));
	if (!sr)
		die_errno("calloc");
	sr->solver = st;
	sr->pkg_cons = calloc(st->nr_packages, sizeof(*sr->pkg_cons));
	if (!sr->pkg_cons)
		die_errno("calloc");
	sr->decided = malloc(st->nr_packages * sizeof(*sr->decided));
	if (!sr->decided)
		die_errno("malloc");
	for (size_t i = 0; i < st->nr_packages; i++)
		sr->decided[i] = -1;
	return sr;
}

static void search_free(struct pg_search *sr)
{
	if (!sr)
		return;
	for (size_t i = 0; i < sr->solver->nr_packages; i++)
		free(sr->pkg_cons[i].entries);
	free(sr->pkg_cons);
	free(sr->decided);
	free(sr->stack);
	free(sr);
}

/* Does version @p vidx of @p pkg_id satisfy every current constraint? */
static int version_ok(const struct pg_search *sr, int pkg_id, int vidx)
{
	const struct pg_package *p = &sr->solver->packages[pkg_id];
	const struct pg_pkg_version *v = &p->versions[vidx];
	const struct pg_cons_list *cl = &sr->pkg_cons[pkg_id];
	for (size_t i = 0; i < cl->nr; i++)
		if (!semver_constraint_matches(cl->entries[i].c, &v->v))
			return 0;
	return 1;
}

/* Does ANY version of @p pkg_id satisfy every current constraint? */
static int has_any_valid(const struct pg_search *sr, int pkg_id)
{
	const struct pg_package *p = &sr->solver->packages[pkg_id];
	for (size_t i = 0; i < p->nr_versions; i++)
		if (version_ok(sr, pkg_id, (int)i))
			return 1;
	return 0;
}

/*
 * Register @p c as a constraint on @p pkg_id, tagged with the decision
 * that introduced it.  Returns 0 on immediate conflict (the new
 * constraint is incompatible with the current state), 1 otherwise.
 * On conflict the constraint remains appended so that drop_constraints()
 * on backtrack can remove it uniformly.
 */
static int add_constraint(struct pg_search *sr, int pkg_id,
			  struct semver_constraint *c, int source_stack_idx)
{
	struct pg_cons_list *cl = &sr->pkg_cons[pkg_id];

	ALLOC_GROW(cl->entries, cl->nr + 1, cl->alloc);
	cl->entries[cl->nr].c = c;
	cl->entries[cl->nr].source_stack_idx = source_stack_idx;
	cl->nr++;

	if (sr->decided[pkg_id] >= 0) {
		int idx = sr->decided[pkg_id];
		const struct pg_pkg_version *v =
		    &sr->solver->packages[pkg_id].versions[idx];
		if (!semver_constraint_matches(c, &v->v))
			return 0;
	}

	if (!has_any_valid(sr, pkg_id))
		return 0;

	return 1;
}

/* Remove every constraint introduced by decision @p source_stack_idx. */
static void drop_constraints(struct pg_search *sr, int source_stack_idx)
{
	for (size_t i = 0; i < sr->solver->nr_packages; i++) {
		struct pg_cons_list *cl = &sr->pkg_cons[i];
		size_t write = 0;
		for (size_t r = 0; r < cl->nr; r++) {
			if (cl->entries[r].source_stack_idx != source_stack_idx)
				cl->entries[write++] = cl->entries[r];
		}
		cl->nr = write;
	}
}

/*
 * Find the next package to decide: any with constraints but no decision.
 * Prefers whichever has the smallest remaining candidate set (fewest
 * viable versions), breaking ties by registration order.  Fast-fails
 * to zero-candidate packages -- they'll provoke immediate backtrack.
 * Returns -1 when every constrained package is decided.
 */
static int pick_next(const struct pg_search *sr)
{
	int best = -1;
	int best_count = INT_MAX;

	for (size_t i = 0; i < sr->solver->nr_packages; i++) {
		if (sr->decided[i] >= 0)
			continue;
		if (sr->pkg_cons[i].nr == 0)
			continue;

		int count = 0;
		const struct pg_package *p = &sr->solver->packages[i];
		for (size_t j = 0; j < p->nr_versions; j++)
			if (version_ok(sr, (int)i, (int)j))
				count++;

		if (count < best_count) {
			best = (int)i;
			best_count = count;
			if (count == 0)
				break;
		}
	}
	return best;
}

static int search_step(struct pg_search *sr);

static int try_version(struct pg_search *sr, int pkg_id, int vidx)
{
	struct pg_package *p = &sr->solver->packages[pkg_id];
	struct pg_pkg_version *v = &p->versions[vidx];
	size_t stack_idx;
	int ok = 1;

	ALLOC_GROW(sr->stack, sr->nr_stack + 1, sr->alloc_stack);
	stack_idx = sr->nr_stack;
	sr->stack[stack_idx].pkg_id = pkg_id;
	sr->stack[stack_idx].version_idx = vidx;
	sr->nr_stack++;
	sr->decided[pkg_id] = vidx;

	for (size_t i = 0; i < v->nr_deps && ok; i++) {
		struct pg_dep_parsed *d = &v->deps[i];
		if (!add_constraint(sr, d->pkg_id, d->c, (int)stack_idx))
			ok = 0;
	}

	if (ok && search_step(sr))
		return 1;

	/* Backtrack. */
	drop_constraints(sr, (int)stack_idx);
	sr->decided[pkg_id] = -1;
	sr->nr_stack--;
	return 0;
}

static int search_step(struct pg_search *sr)
{
	int pkg_id = pick_next(sr);
	struct pg_package *p;

	if (pkg_id < 0)
		return 1; /* solved */

	p = &sr->solver->packages[pkg_id];
	sort_package_versions(p);

	for (size_t i = 0; i < p->nr_versions; i++) {
		if (!version_ok(sr, pkg_id, (int)i))
			continue;
		if (try_version(sr, pkg_id, (int)i))
			return 1;
	}
	return 0;
}

static void fill_solution(struct pg_search *sr, struct pubgrub_solution *out)
{
	struct pubgrub_solution_state *ss;

	ss = calloc(1, sizeof(*ss));
	if (!ss)
		die_errno("calloc");

	for (size_t i = 0; i < sr->solver->nr_packages; i++) {
		int idx = sr->decided[i];
		const struct pg_package *p;
		if (idx < 0)
			continue;
		p = &sr->solver->packages[i];
		ALLOC_GROW(ss->entries, ss->nr + 1, ss->alloc);
		ss->entries[ss->nr].name = sbuf_strdup(p->name);
		ss->entries[ss->nr].version = sbuf_strdup(p->versions[idx].str);
		ss->nr++;
	}
	out->state = ss;
}

int pubgrub_solver_solve(struct pubgrub_solver *s, struct pubgrub_solution *out)
{
	struct pubgrub_solver_state *st;
	struct pg_search *sr;
	int feasible = 1;
	int ok;

	pubgrub_solution_release(out);

	st = ensure_state(s);
	sbuf_reset(&st->error);

	if (st->nr_packages == 0)
		return 0;

	sr = search_new(st);

	for (size_t i = 0; i < st->nr_root_deps; i++) {
		if (!add_constraint(sr, st->root_deps[i].pkg_id,
				    st->root_deps[i].c, -1)) {
			feasible = 0;
			break;
		}
	}

	ok = feasible && search_step(sr);
	if (ok) {
		fill_solution(sr, out);
	} else if (st->error.len == 0) {
		sbuf_addstr(&st->error,
			    "no solution satisfies the given constraints");
	}
	search_free(sr);
	return ok ? 0 : -1;
}

const char *pubgrub_solver_error(const struct pubgrub_solver *s)
{
	if (!s->state)
		return "";
	return s->state->error.buf;
}

/* ==================================================================== */
/* Solution accessors                                                     */
/* ==================================================================== */

void pubgrub_solution_init(struct pubgrub_solution *sol) { sol->state = NULL; }

void pubgrub_solution_release(struct pubgrub_solution *sol)
{
	struct pubgrub_solution_state *ss = sol->state;
	if (!ss)
		return;
	for (size_t i = 0; i < ss->nr; i++) {
		free(ss->entries[i].name);
		free(ss->entries[i].version);
	}
	free(ss->entries);
	free(ss);
	sol->state = NULL;
}

size_t pubgrub_solution_count(const struct pubgrub_solution *sol)
{
	return sol->state ? sol->state->nr : 0;
}

const char *pubgrub_solution_name(const struct pubgrub_solution *sol, size_t i)
{
	return sol->state && i < sol->state->nr ? sol->state->entries[i].name
						: NULL;
}

const char *pubgrub_solution_version(const struct pubgrub_solution *sol,
				     size_t i)
{
	return sol->state && i < sol->state->nr ? sol->state->entries[i].version
						: NULL;
}

const char *pubgrub_solution_version_of(const struct pubgrub_solution *sol,
					const char *name)
{
	if (!sol->state)
		return NULL;
	for (size_t i = 0; i < sol->state->nr; i++)
		if (strcmp(sol->state->entries[i].name, name) == 0)
			return sol->state->entries[i].version;
	return NULL;
}
