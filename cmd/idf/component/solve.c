/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * PubGrub-driven dependency resolver for the component manager.
 * See solve.h for the contract.  The flow at a glance:
 *
 *   1. Walk registry breadth-first from the root deps, collecting
 *      every transitively referenced (name, registry) pair.
 *   2. Register one PubGrub fact per (name, version) discovered.  The
 *      synthetic @c idf package gets a single fact at @p idf_version.
 *   3. Add each manifest root constraint, run the solver, then look
 *      up each chosen version's metadata to assemble @c solve_resolved.
 */
#include "solve.h"

#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "cmd/idf/component/registry.h"
#include "error.h"
#include "pubgrub.h"
#include "sbuf.h"

static char *dup_or_null(const char *s) { return s ? sbuf_strdup(s) : NULL; }

/* -------------------------------------------------------------------- */
/* Cache of fetched registry components                                  */
/* -------------------------------------------------------------------- */

struct fetched_set {
	struct reg_component *items;
	size_t nr, alloc;
};

static void fetched_release(struct fetched_set *fs)
{
	for (size_t i = 0; i < fs->nr; i++)
		reg_release(&fs->items[i]);
	free(fs->items);
	memset(fs, 0, sizeof(*fs));
}

/* -------------------------------------------------------------------- */
/* BFS work queue: pending (name, registry_url) lookups                  */
/* -------------------------------------------------------------------- */

struct queue_item {
	char *name;
	char *registry_url;
};

struct queue {
	struct queue_item *items;
	size_t head, tail, alloc;
};

static void queue_push(struct queue *q, const char *name,
		       const char *registry_url)
{
	ALLOC_GROW(q->items, q->tail + 1, q->alloc);
	q->items[q->tail].name = sbuf_strdup(name);
	q->items[q->tail].registry_url = dup_or_null(registry_url);
	q->tail++;
}

static int queue_pop(struct queue *q, char **name, char **registry_url)
{
	if (q->head == q->tail)
		return 0;
	*name = q->items[q->head].name;
	*registry_url = q->items[q->head].registry_url;
	q->items[q->head].name = NULL;
	q->items[q->head].registry_url = NULL;
	q->head++;
	return 1;
}

static void queue_release(struct queue *q)
{
	for (size_t i = q->head; i < q->tail; i++) {
		free(q->items[i].name);
		free(q->items[i].registry_url);
	}
	free(q->items);
	memset(q, 0, sizeof(*q));
}

/* -------------------------------------------------------------------- */
/* "seen" set                                                            */
/* -------------------------------------------------------------------- */

struct seen_set {
	char **names;
	size_t nr, alloc;
};

static int seen_has(struct seen_set *s, const char *name)
{
	for (size_t i = 0; i < s->nr; i++) {
		if (!strcmp(s->names[i], name))
			return 1;
	}
	return 0;
}

static void seen_add(struct seen_set *s, const char *name)
{
	ALLOC_GROW(s->names, s->nr + 1, s->alloc);
	s->names[s->nr++] = sbuf_strdup(name);
}

static void seen_release(struct seen_set *s)
{
	for (size_t i = 0; i < s->nr; i++)
		free(s->names[i]);
	free(s->names);
	memset(s, 0, sizeof(*s));
}

/* -------------------------------------------------------------------- */
/* BFS                                                                   */
/* -------------------------------------------------------------------- */

/*
 * Fetch every (name, registry_url) reachable from the root deps and
 * stash the parsed responses in @p fs.  The synthetic @c idf package
 * is recorded in @p seen but never fetched.
 */
static void bfs_collect(const struct solve_root *roots, size_t roots_nr,
			const char *default_registry_url,
			struct fetched_set *fs, struct seen_set *seen)
{
	struct queue q = {0};

	for (size_t i = 0; i < roots_nr; i++)
		queue_push(&q, roots[i].name,
			   roots[i].is_idf ? NULL
					   : (roots[i].registry_url
						  ? roots[i].registry_url
						  : default_registry_url));

	for (;;) {
		char *name = NULL, *reg = NULL;
		struct reg_component c = REG_COMPONENT_INIT;

		if (!queue_pop(&q, &name, &reg))
			break;

		if (seen_has(seen, name)) {
			free(name);
			free(reg);
			continue;
		}
		seen_add(seen, name);

		/* Synthetic idf is not in the registry. */
		if (!strcmp(name, "idf")) {
			free(name);
			free(reg);
			continue;
		}

		if (reg_fetch_component(&c, reg ? reg : default_registry_url,
					name) < 0)
			die("cannot fetch component metadata for '%s'", name);

		ALLOC_GROW(fs->items, fs->nr + 1, fs->alloc);
		fs->items[fs->nr++] = c;

		for (size_t v = 0; v < c.versions_nr; v++) {
			for (size_t d = 0; d < c.versions[v].deps_nr; d++) {
				const struct reg_dep *dep =
				    &c.versions[v].deps[d];
				if (seen_has(seen, dep->name))
					continue;
				queue_push(&q, dep->name,
					   dep->is_idf
					       ? NULL
					       : (dep->registry_url
						      ? dep->registry_url
						      : default_registry_url));
			}
		}

		free(name);
		free(reg);
	}
	queue_release(&q);
}

/* -------------------------------------------------------------------- */
/* Solver feed                                                           */
/* -------------------------------------------------------------------- */

static void register_idf(struct pubgrub_solver *s, const char *idf_version)
{
	if (pubgrub_solver_add(s, "idf", idf_version ? idf_version : "0.0.0",
			       NULL, 0) < 0)
		die("pubgrub: cannot register synthetic idf=%s", idf_version);
}

static void register_component(struct pubgrub_solver *s,
			       const struct reg_component *c)
{
	for (size_t v = 0; v < c->versions_nr; v++) {
		const struct reg_version *ver = &c->versions[v];
		struct pubgrub_dep *tmp = NULL;
		size_t n = ver->deps_nr;

		if (n) {
			tmp = calloc(n, sizeof(*tmp));
			if (!tmp)
				die_errno("calloc");
			for (size_t i = 0; i < n; i++) {
				tmp[i].name = ver->deps[i].name;
				tmp[i].spec = ver->deps[i].spec;
			}
		}

		/* Malformed semver / spec: skip this version rather than
		 * abort -- the registry occasionally publishes pre-release
		 * variants the parser rejects.  The remaining versions
		 * still get a chance to satisfy the constraint. */
		(void)pubgrub_solver_add(s, c->name, ver->version, tmp, n);
		free(tmp);
	}
}

/* -------------------------------------------------------------------- */
/* Solution -> resolved-array assembly                                    */
/* -------------------------------------------------------------------- */

/* Find the @c reg_version pointer for @p name = @p ver inside @p fs. */
static const struct reg_version *find_version(const struct fetched_set *fs,
					      const char *name, const char *ver)
{
	for (size_t i = 0; i < fs->nr; i++) {
		const struct reg_component *c = &fs->items[i];
		if (strcmp(c->name, name) != 0)
			continue;
		for (size_t v = 0; v < c->versions_nr; v++) {
			if (!strcmp(c->versions[v].version, ver))
				return &c->versions[v];
		}
		return NULL;
	}
	return NULL;
}

static const struct reg_component *find_component(const struct fetched_set *fs,
						  const char *name)
{
	for (size_t i = 0; i < fs->nr; i++) {
		if (!strcmp(fs->items[i].name, name))
			return &fs->items[i];
	}
	return NULL;
}

static void assemble_nested(const struct reg_version *ver,
			    struct lockfile_nested_dep **out, size_t *out_nr)
{
	struct lockfile_nested_dep *arr = NULL;

	if (!ver || ver->deps_nr == 0)
		return;

	arr = calloc(ver->deps_nr, sizeof(*arr));
	if (!arr)
		die_errno("calloc");
	for (size_t i = 0; i < ver->deps_nr; i++) {
		arr[i].name = sbuf_strdup(ver->deps[i].name);
		arr[i].version = sbuf_strdup(ver->deps[i].spec);
		/* The registry's @c is_public is per-dep but the lockfile
		 * stores public/private under @c require.  Default to
		 * "private" -- callers can polish later. */
		arr[i].require = sbuf_strdup("private");
	}
	*out = arr;
	*out_nr = ver->deps_nr;
}

/* -------------------------------------------------------------------- */
/* Public entry point                                                    */
/* -------------------------------------------------------------------- */

int solve_resolve(const struct solve_root *roots, size_t roots_nr,
		  const char *idf_version, const char *default_registry_url,
		  struct solve_resolved **out, size_t *out_nr, struct sbuf *err)
{
	struct fetched_set fs = {0};
	struct seen_set seen = {0};
	struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
	struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;
	struct solve_resolved *result = NULL;
	size_t result_nr = 0, result_alloc = 0;
	int rc = -1;

	bfs_collect(roots, roots_nr, default_registry_url, &fs, &seen);

	register_idf(&s, idf_version);
	for (size_t i = 0; i < fs.nr; i++)
		register_component(&s, &fs.items[i]);

	for (size_t i = 0; i < roots_nr; i++) {
		if (pubgrub_solver_root_dep(&s, roots[i].name, roots[i].spec) <
		    0) {
			if (err)
				sbuf_addf(err,
					  "invalid version constraint "
					  "for %s: %s",
					  roots[i].name, roots[i].spec);
			goto out;
		}
	}

	if (pubgrub_solver_solve(&s, &sol) != 0) {
		if (err)
			sbuf_addstr(err, pubgrub_solver_error(&s));
		goto out;
	}

	for (size_t i = 0; i < pubgrub_solution_count(&sol); i++) {
		const char *name = pubgrub_solution_name(&sol, i);
		const char *ver = pubgrub_solution_version(&sol, i);
		struct solve_resolved e = {0};

		e.name = sbuf_strdup(name);
		e.version = sbuf_strdup(ver);

		if (!strcmp(name, "idf")) {
			e.is_idf = 1;
		} else {
			const struct reg_component *c =
			    find_component(&fs, name);
			const struct reg_version *rv =
			    find_version(&fs, name, ver);
			if (c)
				e.registry_url = sbuf_strdup(c->registry_url);
			if (rv) {
				e.download_url = dup_or_null(rv->download_url);
				e.component_hash =
				    dup_or_null(rv->component_hash);
				assemble_nested(rv, &e.nested, &e.nested_nr);
			}
		}

		ALLOC_GROW(result, result_nr + 1, result_alloc);
		result[result_nr++] = e;
	}

	*out = result;
	*out_nr = result_nr;
	rc = 0;

out:
	if (rc != 0) {
		for (size_t i = 0; i < result_nr; i++)
			solve_resolved_release(&result[i]);
		free(result);
	}
	pubgrub_solution_release(&sol);
	pubgrub_solver_release(&s);
	fetched_release(&fs);
	seen_release(&seen);
	return rc;
}

void solve_resolved_release(struct solve_resolved *r)
{
	if (!r)
		return;
	free(r->name);
	free(r->version);
	free(r->registry_url);
	free(r->download_url);
	free(r->component_hash);
	for (size_t i = 0; i < r->nested_nr; i++) {
		free(r->nested[i].name);
		free(r->nested[i].version);
		free(r->nested[i].require);
	}
	free(r->nested);
	memset(r, 0, sizeof(*r));
}
