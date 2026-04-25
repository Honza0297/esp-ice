/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for cmd/idf/component/lockfile.c.
 *
 * Golden input was captured by running `idf.py reconfigure` on
 * examples/build_system/cmake/component_manager; the round-trip test
 * verifies that we produce a semantically equivalent representation
 * (parse -> save -> parse -> same structure).
 */
#include "cmd/idf/component/lockfile.h"
#include "ice.h"
#include "tap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse(struct lockfile *lf, const char *s)
{
	return lockfile_parse(lf, s, strlen(s));
}

static const struct lockfile_entry *find(const struct lockfile *lf,
					 const char *name)
{
	for (size_t i = 0; i < lf->nr; i++)
		if (!strcmp(lf->entries[i].name, name))
			return &lf->entries[i];
	return NULL;
}

int main(void)
{
	/* Real Python-tool output from
	 * examples/build_system/cmake/component_manager. */
	{
		struct lockfile lf = LOCKFILE_INIT;
		const char *yml =
		    "dependencies:\n"
		    "  example/cmp:\n"
		    "    component_hash: "
		    "c29f41d59ec860dff32ffa68a20b98b06928dc2111595c5dae368cddc6"
		    "16657c\n"
		    "    dependencies:\n"
		    "    - name: idf\n"
		    "      require: private\n"
		    "      version: '>=4.1'\n"
		    "    source:\n"
		    "      registry_url: https://components.espressif.com/\n"
		    "      type: service\n"
		    "    version: 3.3.9~1\n"
		    "  idf:\n"
		    "    source:\n"
		    "      type: idf\n"
		    "    version: 6.1.0\n"
		    "direct_dependencies:\n"
		    "- example/cmp\n"
		    "- idf\n"
		    "manifest_hash: "
		    "139a0e908bf372b33fc7a6b1c371e8724521c44d89e8f3d8d7bde3c857"
		    "cf86ae\n"
		    "target: esp32\n"
		    "version: 3.0.0\n";
		tap_check(parse(&lf, yml) == 0);

		tap_check(lf.lock_version && !strcmp(lf.lock_version, "3.0.0"));
		tap_check(lf.target && !strcmp(lf.target, "esp32"));
		tap_check(lf.manifest_hash &&
			  !strcmp(lf.manifest_hash,
				  "139a0e908bf372b33fc7a6b1c371e8724521c44d89e8"
				  "f3d8d7bde3c857cf86ae"));
		tap_check(lf.direct_dependencies_nr == 2);
		tap_check(!strcmp(lf.direct_dependencies[0], "example/cmp"));
		tap_check(!strcmp(lf.direct_dependencies[1], "idf"));
		tap_check(lf.nr == 2);

		const struct lockfile_entry *cmp = find(&lf, "example/cmp");
		tap_check(cmp && cmp->src_type == LOCKFILE_SRC_SERVICE);
		tap_check(!strcmp(cmp->version, "3.3.9~1"));
		tap_check(!strcmp(cmp->registry_url,
				  "https://components.espressif.com/"));
		tap_check(!strcmp(cmp->component_hash,
				  "c29f41d59ec860dff32ffa68a20b98b06928dc211159"
				  "5c5dae368cddc616657c"));
		tap_check(cmp->nested_nr == 1);
		tap_check(!strcmp(cmp->nested[0].name, "idf"));
		tap_check(!strcmp(cmp->nested[0].require, "private"));
		tap_check(!strcmp(cmp->nested[0].version, ">=4.1"));

		const struct lockfile_entry *idf = find(&lf, "idf");
		tap_check(idf && idf->src_type == LOCKFILE_SRC_IDF);
		tap_check(!strcmp(idf->version, "6.1.0"));

		lockfile_release(&lf);
		tap_done("parse real Python-tool lockfile");
	}

	/* Git source variant. */
	{
		struct lockfile lf = LOCKFILE_INIT;
		const char *yml =
		    "version: \"3.0.0\"\n"
		    "target: esp32s3\n"
		    "dependencies:\n"
		    "  u8g2:\n"
		    "    version: a549a13b\n"
		    "    source:\n"
		    "      type: git\n"
		    "      git_url: https://github.com/olikraus/u8g2.git\n"
		    "      git_ref: a549a13b\n";
		tap_check(parse(&lf, yml) == 0);
		tap_check(lf.nr == 1);
		const struct lockfile_entry *g = find(&lf, "u8g2");
		tap_check(g && g->src_type == LOCKFILE_SRC_GIT);
		tap_check(!strcmp(g->git_url,
				  "https://github.com/olikraus/u8g2.git"));
		tap_check(!strcmp(g->git_ref, "a549a13b"));
		lockfile_release(&lf);
		tap_done("parse git source");
	}

	/* Unknown source type parses as UNKNOWN (forward-compatible). */
	{
		struct lockfile lf = LOCKFILE_INIT;
		const char *yml = "version: \"3.0.0\"\n"
				  "dependencies:\n"
				  "  future:\n"
				  "    version: 1.0.0\n"
				  "    source:\n"
				  "      type: oci\n";
		tap_check(parse(&lf, yml) == 0);
		tap_check(lf.nr == 1);
		tap_check(lf.entries[0].src_type == LOCKFILE_SRC_UNKNOWN);
		lockfile_release(&lf);
		tap_done("unknown source type parses as UNKNOWN");
	}

	/* Round-trip: save then parse, structure matches.  Not asserting
	 * byte-identity with Python output -- see lockfile.h. */
	{
		struct lockfile a = LOCKFILE_INIT;
		struct lockfile b = LOCKFILE_INIT;
		const char *path = "roundtrip.lock";

		a.lock_version = sbuf_strdup("3.0.0");
		a.target = sbuf_strdup("esp32s3");
		a.manifest_hash = sbuf_strdup("deadbeefcafe");
		a.direct_dependencies =
		    calloc(1, sizeof(*a.direct_dependencies));
		a.direct_dependencies[0] = sbuf_strdup("espressif/cjson");
		a.direct_dependencies_nr = 1;
		a.entries = calloc(2, sizeof(*a.entries));
		a.nr = 2;
		a.entries[0].name = sbuf_strdup("espressif/cjson");
		a.entries[0].version = sbuf_strdup("1.7.14");
		a.entries[0].src_type = LOCKFILE_SRC_SERVICE;
		a.entries[0].registry_url =
		    sbuf_strdup("https://components.espressif.com/");
		a.entries[0].component_hash = sbuf_strdup("feedface");
		a.entries[1].name = sbuf_strdup("idf");
		a.entries[1].version = sbuf_strdup("5.3.1");
		a.entries[1].src_type = LOCKFILE_SRC_IDF;

		tap_check(lockfile_save(&a, path) == 0);
		tap_check(lockfile_load(&b, path) == 0);

		tap_check(!strcmp(b.lock_version, "3.0.0"));
		tap_check(!strcmp(b.target, "esp32s3"));
		tap_check(!strcmp(b.manifest_hash, "deadbeefcafe"));
		tap_check(b.direct_dependencies_nr == 1);
		tap_check(!strcmp(b.direct_dependencies[0], "espressif/cjson"));
		tap_check(b.nr == 2);

		const struct lockfile_entry *w = find(&b, "espressif/cjson");
		tap_check(w && w->src_type == LOCKFILE_SRC_SERVICE);
		tap_check(!strcmp(w->component_hash, "feedface"));
		tap_check(!strcmp(w->registry_url,
				  "https://components.espressif.com/"));

		const struct lockfile_entry *i = find(&b, "idf");
		tap_check(i && i->src_type == LOCKFILE_SRC_IDF);
		tap_check(!strcmp(i->version, "5.3.1"));

		lockfile_release(&a);
		lockfile_release(&b);
		tap_done("save + reload round-trip");
	}

	/* Non-lockfile input rejected. */
	{
		struct lockfile lf = LOCKFILE_INIT;
		tap_check(parse(&lf, "not a lockfile\n") == -1);
		tap_done("non-map root rejected");
	}

	return tap_result();
}
