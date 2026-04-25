/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * CMake output emitters for the component-manager hooks.  See
 * cmake_out.h for the contract.  Internally: assemble everything into
 * an sbuf, then write the whole file atomically so a half-written
 * temp file can never be observed by CMake.
 */
#include "cmake_out.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "fs.h"
#include "sbuf.h"

static int cmp_dl_by_name(const void *a, const void *b)
{
	const struct cmake_dl_component *da = a;
	const struct cmake_dl_component *db = b;
	return strcmp(da->name, db->name);
}

static int cmp_cstr(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int cmake_out_emit_managed_list(const char *path,
				const struct cmake_local_component *locals,
				size_t nr_locals,
				const struct cmake_dl_component *downloads,
				size_t nr_downloads)
{
	struct sbuf sb = SBUF_INIT;
	struct cmake_dl_component *sorted = NULL;
	int rc;

	for (size_t i = 0; i < nr_locals; i++) {
		if (!locals[i].version)
			continue;
		sbuf_addf(&sb,
			  "idf_component_set_property(%s "
			  "COMPONENT_VERSION \"%s\")\n",
			  locals[i].name, locals[i].version);
	}

	if (nr_downloads) {
		sorted = malloc(nr_downloads * sizeof(*sorted));
		if (!sorted)
			die_errno("malloc");
		memcpy(sorted, downloads, nr_downloads * sizeof(*sorted));
		qsort(sorted, nr_downloads, sizeof(*sorted), cmp_dl_by_name);
	}

	for (size_t i = 0; i < nr_downloads; i++) {
		const struct cmake_dl_component *d = &sorted[i];

		sbuf_addf(&sb,
			  "idf_build_component(\"%s\" "
			  "\"project_managed_components\")\n",
			  d->abs_path);
		sbuf_addf(&sb,
			  "idf_component_set_property(%s "
			  "COMPONENT_VERSION \"%s\")\n",
			  d->name, d->version);

		if (d->targets_nr) {
			sbuf_addf(&sb,
				  "idf_component_set_property(%s "
				  "REQUIRED_IDF_TARGETS \"",
				  d->name);
			for (size_t j = 0; j < d->targets_nr; j++) {
				if (j)
					sbuf_addch(&sb, ' ');
				sbuf_addstr(&sb, d->targets[j]);
			}
			sbuf_addstr(&sb, "\")\n");
		}
	}

	sbuf_addstr(&sb, "set(managed_components \"");
	for (size_t i = 0; i < nr_downloads; i++) {
		if (i)
			sbuf_addch(&sb, ';');
		sbuf_addstr(&sb, sorted[i].name);
	}
	sbuf_addstr(&sb, "\")\n");

	rc = write_file_atomic(path, sb.buf, sb.len);

	free(sorted);
	sbuf_release(&sb);
	return rc;
}

int cmake_out_emit_components_paths(const char *path, const char *const *paths,
				    size_t nr)
{
	struct sbuf sb = SBUF_INIT;
	const char **sorted = NULL;
	int rc;

	if (nr) {
		sorted = malloc(nr * sizeof(*sorted));
		if (!sorted)
			die_errno("malloc");
		memcpy(sorted, paths, nr * sizeof(*sorted));
		qsort(sorted, nr, sizeof(*sorted), cmp_cstr);
	}

	for (size_t i = 0; i < nr; i++) {
		if (i)
			sbuf_addch(&sb, '\n');
		sbuf_addstr(&sb, sorted[i]);
	}

	rc = write_file_atomic(path, sb.buf, sb.len);

	free(sorted);
	sbuf_release(&sb);
	return rc;
}
