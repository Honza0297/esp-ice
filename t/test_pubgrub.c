/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for pubgrub.c -- dependency resolver.
 *
 * Cases are translated from the canonical PubGrub test corpus
 * (originally from Dart pub's resolver tests, also carried in
 * mixology/Poetry and idf-component-manager).  We check decision
 * maps only; the Python tests' `tries=N` assertions are
 * PubGrub-search-order specific and not part of the ice contract
 * (any correct solver is allowed to reach the solution via a
 * different path).
 */
#include "ice.h"
#include "pubgrub.h"
#include "tap.h"

#include <string.h>

/* Helper: "solution has pkg @ ver". */
static int has(const struct pubgrub_solution *sol, const char *name,
	       const char *ver)
{
	const char *v = pubgrub_solution_version_of(sol, name);
	return v && strcmp(v, ver) == 0;
}

int main(void)
{
	/* Empty solver -- no root deps, no packages.  Trivially solved
	 * with an empty solution. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;
		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(pubgrub_solution_count(&sol) == 0);
		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("empty solver: trivial success");
	}

	/* Simple dependencies: root a=1.0.0, b=1.0.0.  Each pulls two
	 * leaves. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", "1.0.0");
		pubgrub_solver_root_dep(&s, "b", "1.0.0");
		pubgrub_solver_add(
		    &s, "a", "1.0.0",
		    (struct pubgrub_dep[]){{"aa", "1.0.0"}, {"ab", "1.0.0"}},
		    2);
		pubgrub_solver_add(
		    &s, "b", "1.0.0",
		    (struct pubgrub_dep[]){{"ba", "1.0.0"}, {"bb", "1.0.0"}},
		    2);
		pubgrub_solver_add(&s, "aa", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "ab", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "ba", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "bb", "1.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "a", "1.0.0"));
		tap_check(has(&sol, "b", "1.0.0"));
		tap_check(has(&sol, "aa", "1.0.0"));
		tap_check(has(&sol, "ab", "1.0.0"));
		tap_check(has(&sol, "ba", "1.0.0"));
		tap_check(has(&sol, "bb", "1.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("simple dependencies");
	}

	/* Shared dependency with overlapping ranges: intersection picks
	 * the highest version in the overlap. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", "1.0.0");
		pubgrub_solver_root_dep(&s, "b", "1.0.0");
		pubgrub_solver_add(
		    &s, "a", "1.0.0",
		    (struct pubgrub_dep[]){{"shared", ">=2.0.0,<4.0.0"}}, 1);
		pubgrub_solver_add(
		    &s, "b", "1.0.0",
		    (struct pubgrub_dep[]){{"shared", ">=3.0.0,<5.0.0"}}, 1);
		pubgrub_solver_add(&s, "shared", "2.0.0", NULL, 0);
		pubgrub_solver_add(&s, "shared", "3.0.0", NULL, 0);
		pubgrub_solver_add(&s, "shared", "3.6.9", NULL, 0);
		pubgrub_solver_add(&s, "shared", "4.0.0", NULL, 0);
		pubgrub_solver_add(&s, "shared", "5.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "shared", "3.6.9"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("shared dependency overlapping ranges");
	}

	/* Dependency version affects other dependencies: root foo<=1.0.2
	 * and bar=1.0.0.  bar limits foo to <=1.0.1, so foo=1.0.1 wins
	 * and only its dep (bang) is chosen. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "foo", "<=1.0.2");
		pubgrub_solver_root_dep(&s, "bar", "1.0.0");
		pubgrub_solver_add(&s, "foo", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "foo", "1.0.1",
				   (struct pubgrub_dep[]){{"bang", "1.0.0"}},
				   1);
		pubgrub_solver_add(&s, "foo", "1.0.2",
				   (struct pubgrub_dep[]){{"whoop", "1.0.0"}},
				   1);
		pubgrub_solver_add(&s, "foo", "1.0.3",
				   (struct pubgrub_dep[]){{"zoop", "1.0.0"}},
				   1);
		pubgrub_solver_add(&s, "bar", "1.0.0",
				   (struct pubgrub_dep[]){{"foo", "<=1.0.1"}},
				   1);
		pubgrub_solver_add(&s, "bang", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "whoop", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "zoop", "1.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "foo", "1.0.1"));
		tap_check(has(&sol, "bar", "1.0.0"));
		tap_check(has(&sol, "bang", "1.0.0"));
		tap_check(pubgrub_solution_version_of(&sol, "whoop") == NULL);
		tap_check(pubgrub_solution_version_of(&sol, "zoop") == NULL);

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("dep version affects other deps");
	}

	/* Circular dependency: foo@1 -> bar@1 -> foo@1. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "foo", "1.0.0");
		pubgrub_solver_add(&s, "foo", "1.0.0",
				   (struct pubgrub_dep[]){{"bar", "1.0.0"}}, 1);
		pubgrub_solver_add(&s, "bar", "1.0.0",
				   (struct pubgrub_dep[]){{"foo", "1.0.0"}}, 1);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "foo", "1.0.0"));
		tap_check(has(&sol, "bar", "1.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("circular dependency");
	}

	/* Circular dep on older version: a@2 -> b@1 -> a@1; solver
	 * should pick a=1.0 (no deps) rather than oscillate. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", ">=1.0.0");
		pubgrub_solver_add(&s, "a", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "a", "2.0.0",
				   (struct pubgrub_dep[]){{"b", "1.0.0"}}, 1);
		pubgrub_solver_add(&s, "b", "1.0.0",
				   (struct pubgrub_dep[]){{"a", "1.0.0"}}, 1);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "a", "1.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("circular dep resolved on older version");
	}

	/* Diamond conflict: a=2 needs c^1, b=2 needs c^3; greedy would
	 * get stuck.  Correct answer is a=1, b=2, c=3. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", "*");
		pubgrub_solver_root_dep(&s, "b", "*");
		pubgrub_solver_add(&s, "a", "2.0.0",
				   (struct pubgrub_dep[]){{"c", "^1.0.0"}}, 1);
		pubgrub_solver_add(&s, "a", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "b", "2.0.0",
				   (struct pubgrub_dep[]){{"c", "^3.0.0"}}, 1);
		pubgrub_solver_add(&s, "b", "1.0.0",
				   (struct pubgrub_dep[]){{"c", "^2.0.0"}}, 1);
		pubgrub_solver_add(&s, "c", "3.0.0", NULL, 0);
		pubgrub_solver_add(&s, "c", "2.0.0", NULL, 0);
		pubgrub_solver_add(&s, "c", "1.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "a", "1.0.0"));
		tap_check(has(&sol, "b", "2.0.0"));
		tap_check(has(&sol, "c", "3.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("diamond dependency graph");
	}

	/* Backjumps after partial satisfier: c=2 pulls in a,b which jointly
	 * force x=1, which pulls y=1, contradicting root's y^2.  Solver
	 * must backjump past the c decision and pick c=1 instead. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "c", "*");
		pubgrub_solver_root_dep(&s, "y", "^2.0.0");

		pubgrub_solver_add(&s, "a", "1.0.0",
				   (struct pubgrub_dep[]){{"x", ">=1.0.0"}}, 1);
		pubgrub_solver_add(&s, "b", "1.0.0",
				   (struct pubgrub_dep[]){{"x", "<2.0.0"}}, 1);
		pubgrub_solver_add(&s, "c", "1.0.0", NULL, 0);
		pubgrub_solver_add(
		    &s, "c", "2.0.0",
		    (struct pubgrub_dep[]){{"a", "*"}, {"b", "*"}}, 2);

		pubgrub_solver_add(&s, "x", "0.0.0", NULL, 0);
		pubgrub_solver_add(&s, "x", "1.0.0",
				   (struct pubgrub_dep[]){{"y", "1.0.0"}}, 1);
		pubgrub_solver_add(&s, "x", "2.0.0", NULL, 0);

		pubgrub_solver_add(&s, "y", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "y", "2.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "c", "1.0.0"));
		tap_check(has(&sol, "y", "2.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("backjumps after partial satisfier");
	}

	/* Rolls back leaf versions first: picking a=2 forces c=2; b=2
	 * would require c=1 (conflict), so b must downgrade to 1. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", "*");
		pubgrub_solver_add(&s, "a", "1.0.0",
				   (struct pubgrub_dep[]){{"b", "*"}}, 1);
		pubgrub_solver_add(
		    &s, "a", "2.0.0",
		    (struct pubgrub_dep[]){{"b", "*"}, {"c", "2.0.0"}}, 2);
		pubgrub_solver_add(&s, "b", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "b", "2.0.0",
				   (struct pubgrub_dep[]){{"c", "1.0.0"}}, 1);
		pubgrub_solver_add(&s, "c", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "c", "2.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "a", "2.0.0"));
		tap_check(has(&sol, "b", "1.0.0"));
		tap_check(has(&sol, "c", "2.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("rolls back leaf versions first");
	}

	/* Simple transitive: only baz=1 exists, so foo and bar must
	 * downgrade through the chain until they land on versions
	 * compatible with it. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "foo", "*");
		pubgrub_solver_add(&s, "foo", "1.0.0",
				   (struct pubgrub_dep[]){{"bar", "1.0.0"}}, 1);
		pubgrub_solver_add(&s, "foo", "2.0.0",
				   (struct pubgrub_dep[]){{"bar", "2.0.0"}}, 1);
		pubgrub_solver_add(&s, "foo", "3.0.0",
				   (struct pubgrub_dep[]){{"bar", "3.0.0"}}, 1);
		pubgrub_solver_add(&s, "bar", "1.0.0",
				   (struct pubgrub_dep[]){{"baz", "*"}}, 1);
		pubgrub_solver_add(&s, "bar", "2.0.0",
				   (struct pubgrub_dep[]){{"baz", "2.0.0"}}, 1);
		pubgrub_solver_add(&s, "bar", "3.0.0",
				   (struct pubgrub_dep[]){{"baz", "3.0.0"}}, 1);
		pubgrub_solver_add(&s, "baz", "1.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "foo", "1.0.0"));
		tap_check(has(&sol, "bar", "1.0.0"));
		tap_check(has(&sol, "baz", "1.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("simple transitive downgrade");
	}

	/* Backjump to nearer unsatisfied: a=2 wants a c version that
	 * does not exist; backjump over b's versions. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", "*");
		pubgrub_solver_root_dep(&s, "b", "*");
		pubgrub_solver_add(&s, "a", "1.0.0",
				   (struct pubgrub_dep[]){{"c", "1.0.0"}}, 1);
		pubgrub_solver_add(
		    &s, "a", "2.0.0",
		    (struct pubgrub_dep[]){{"c", "2.0.0-nonexistent"}}, 1);
		pubgrub_solver_add(&s, "b", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "b", "2.0.0", NULL, 0);
		pubgrub_solver_add(&s, "b", "3.0.0", NULL, 0);
		pubgrub_solver_add(&s, "c", "1.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "a", "1.0.0"));
		tap_check(has(&sol, "b", "3.0.0"));
		tap_check(has(&sol, "c", "1.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("backjump to nearer unsatisfied");
	}

	/* Unsolvable: no version of foo matches ^1.0. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "foo", "^1.0");
		pubgrub_solver_add(&s, "foo", "2.0.0", NULL, 0);
		pubgrub_solver_add(&s, "foo", "2.1.3", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == -1);
		tap_check(pubgrub_solver_error(&s)[0] != '\0');
		tap_check(pubgrub_solution_count(&sol) == 0);

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("unsolvable: no matching version");
	}

	/* Unsolvable: root asks for two disjoint versions of the same
	 * package. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "foo", "1.0.0");
		pubgrub_solver_root_dep(&s, "foo", "2.0.0");
		pubgrub_solver_add(&s, "foo", "1.0.0", NULL, 0);
		pubgrub_solver_add(&s, "foo", "2.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == -1);

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("unsolvable: disjoint root constraints");
	}

	/* Unsolvable: mutually exclusive transitive deps with no escape. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", "*");
		pubgrub_solver_root_dep(&s, "b", "*");
		pubgrub_solver_add(&s, "a", "1.0.0",
				   (struct pubgrub_dep[]){{"b", "1.0.0"}}, 1);
		pubgrub_solver_add(&s, "a", "2.0.0",
				   (struct pubgrub_dep[]){{"b", "2.0.0"}}, 1);
		pubgrub_solver_add(&s, "b", "1.0.0",
				   (struct pubgrub_dep[]){{"a", "2.0.0"}}, 1);
		pubgrub_solver_add(&s, "b", "2.0.0",
				   (struct pubgrub_dep[]){{"a", "1.0.0"}}, 1);

		tap_check(pubgrub_solver_solve(&s, &sol) == -1);

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("unsolvable: no valid solution");
	}

	/* API: invalid inputs rejected. */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;

		tap_check(pubgrub_solver_root_dep(&s, "a", "not-a-spec") < 0);
		tap_check(pubgrub_solver_error(&s)[0] != '\0');
		tap_check(
		    pubgrub_solver_add(&s, "a", "not-a-version", NULL, 0) < 0);
		tap_check(pubgrub_solver_add(&s, "a", "1.0.0", NULL, 0) == 0);
		tap_check(pubgrub_solver_add(&s, "a", "1.0.0", NULL, 0) < 0);

		pubgrub_solver_release(&s);
		tap_done("API: invalid inputs rejected");
	}

	/* API: solver can be re-solved after release (reuse pattern). */
	{
		struct pubgrub_solver s = PUBGRUB_SOLVER_INIT;
		struct pubgrub_solution sol = PUBGRUB_SOLUTION_INIT;

		pubgrub_solver_root_dep(&s, "a", "*");
		pubgrub_solver_add(&s, "a", "1.0.0", NULL, 0);

		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "a", "1.0.0"));

		/* Re-solve without releasing anything -- should be idempotent.
		 */
		tap_check(pubgrub_solver_solve(&s, &sol) == 0);
		tap_check(has(&sol, "a", "1.0.0"));

		pubgrub_solution_release(&sol);
		pubgrub_solver_release(&s);
		tap_done("API: repeated solve is idempotent");
	}

	return tap_result();
}
