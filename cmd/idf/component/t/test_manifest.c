/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for cmd/idf/component/manifest.c.
 *
 * Cases cover each dependency form seen in ESP-IDF examples: simple
 * scalar, block-form version, path, git, rules/if, plus the
 * top-level targets list.
 */
#include "cmd/idf/component/manifest.h"
#include "ice.h"
#include "tap.h"

#include <string.h>

static int parse(struct manifest *out, const char *s)
{
	return manifest_parse(out, s, strlen(s));
}

/* Find a dep by name; NULL if missing. */
static const struct manifest_dep *find(const struct manifest *m,
				       const char *name)
{
	for (size_t i = 0; i < m->deps_nr; i++)
		if (!strcmp(m->deps[i].name, name))
			return &m->deps[i];
	return NULL;
}

int main(void)
{
	/* Bare simple form: version string as the value. */
	{
		struct manifest m = MANIFEST_INIT;
		const char *yml = "dependencies:\n"
				  "  idf: \">=4.1\"\n"
				  "  example/cmp: \"^3.3.3\"\n";
		tap_check(parse(&m, yml) == 0);
		tap_check(m.deps_nr == 2);

		const struct manifest_dep *idf = find(&m, "idf");
		tap_check(idf && !strcmp(idf->spec, ">=4.1"));
		tap_check(idf->source == MANIFEST_SRC_REGISTRY);
		tap_check(idf->is_public == -1);

		const struct manifest_dep *cmp = find(&m, "example/cmp");
		tap_check(cmp && !strcmp(cmp->spec, "^3.3.3"));

		manifest_release(&m);
		tap_done("parse simple dependency form");
	}

	/* Block form with version + public + pre_release. */
	{
		struct manifest m = MANIFEST_INIT;
		const char *yml = "dependencies:\n"
				  "  cjson:\n"
				  "    version: \"^1.7\"\n"
				  "    public: true\n"
				  "    pre_release: false\n";
		tap_check(parse(&m, yml) == 0);
		const struct manifest_dep *d = find(&m, "cjson");
		tap_check(d && !strcmp(d->spec, "^1.7"));
		tap_check(d->is_public == 1);
		tap_check(d->pre_release == 0);

		manifest_release(&m);
		tap_done("parse block form with public / pre_release");
	}

	/* Path-based dep. */
	{
		struct manifest m = MANIFEST_INIT;
		const char *yml = "dependencies:\n"
				  "  protocol_examples_common:\n"
				  "    path: "
				  "\"${IDF_PATH}/examples/common_components/"
				  "protocol_examples_common\"\n";
		tap_check(parse(&m, yml) == 0);
		const struct manifest_dep *d =
		    find(&m, "protocol_examples_common");
		tap_check(d && d->source == MANIFEST_SRC_PATH);
		tap_check(d->path && strstr(d->path, "${IDF_PATH}"));
		tap_check(d->spec == NULL);

		manifest_release(&m);
		tap_done("parse path dependency (unexpanded ${IDF_PATH})");
	}

	/* Git-based dep. */
	{
		struct manifest m = MANIFEST_INIT;
		const char *yml =
		    "dependencies:\n"
		    "  u8g2:\n"
		    "    git: \"https://github.com/olikraus/u8g2.git\"\n"
		    "    version: "
		    "\"a549a13b13b5fd568111557c1dd7ca3d06fbe21a\"\n";
		tap_check(parse(&m, yml) == 0);
		const struct manifest_dep *d = find(&m, "u8g2");
		tap_check(d && d->source == MANIFEST_SRC_GIT);
		tap_check(d->git_url &&
			  !strcmp(d->git_url,
				  "https://github.com/olikraus/u8g2.git"));
		tap_check(d->spec &&
			  !strcmp(d->spec,
				  "a549a13b13b5fd568111557c1dd7ca3d06fbe21a"));

		manifest_release(&m);
		tap_done("parse git dependency");
	}

	/* Dep with rules: - if:. */
	{
		struct manifest m = MANIFEST_INIT;
		const char *yml = "dependencies:\n"
				  "  espressif/ethernet_init:\n"
				  "    version: \"^1.3.0\"\n"
				  "    rules:\n"
				  "     - if: \"target != linux\"\n";
		tap_check(parse(&m, yml) == 0);
		const struct manifest_dep *d =
		    find(&m, "espressif/ethernet_init");
		tap_check(d && d->rules_nr == 1);
		tap_check(d->rules[0].if_expr &&
			  !strcmp(d->rules[0].if_expr, "target != linux"));

		manifest_release(&m);
		tap_done("parse dependency with rules[if]");
	}

	/* Service URL override. */
	{
		struct manifest m = MANIFEST_INIT;
		const char *yml =
		    "dependencies:\n"
		    "  my_comp:\n"
		    "    version: \"^2.0\"\n"
		    "    service_url: \"https://registry.internal/api\"\n";
		tap_check(parse(&m, yml) == 0);
		const struct manifest_dep *d = find(&m, "my_comp");
		tap_check(
		    d && d->service_url &&
		    !strcmp(d->service_url, "https://registry.internal/api"));
		tap_check(d->source == MANIFEST_SRC_REGISTRY);

		manifest_release(&m);
		tap_done("parse registry dep with service_url override");
	}

	/* Top-level version + description + targets list. */
	{
		struct manifest m = MANIFEST_INIT;
		const char *yml = "version: \"2.1.0\"\n"
				  "description: \"Example\"\n"
				  "targets:\n"
				  "  - esp32\n"
				  "  - esp32s3\n"
				  "dependencies:\n"
				  "  idf: \">=5.0\"\n";
		tap_check(parse(&m, yml) == 0);
		tap_check(m.version && !strcmp(m.version, "2.1.0"));
		tap_check(m.description && !strcmp(m.description, "Example"));
		tap_check(m.targets_nr == 2);
		tap_check(!strcmp(m.targets[0], "esp32"));
		tap_check(!strcmp(m.targets[1], "esp32s3"));
		tap_check(m.deps_nr == 1);

		manifest_release(&m);
		tap_done("parse version / description / targets");
	}

	/* Empty manifest (just "dependencies:" without children is also fine).
	 */
	{
		struct manifest m = MANIFEST_INIT;
		tap_check(parse(&m, "version: \"1.0.0\"\n") == 0);
		tap_check(m.deps_nr == 0);
		tap_check(m.targets_nr == 0);
		manifest_release(&m);
		tap_done("empty (no dependencies section)");
	}

	/* Non-manifest input rejected: a top-level scalar is not a map. */
	{
		struct manifest m = MANIFEST_INIT;
		tap_check(parse(&m, "not a manifest\n") == -1);
		tap_done("non-map root rejected");
	}

	return tap_result();
}
