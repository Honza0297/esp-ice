/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * dependencies.lock (v3.0.0) parser and writer.
 *
 * The on-disk shape was captured from the Python tool running against
 * a real project -- see lockfile.h for the full schema.  Fields we
 * don't recognise are tolerated at parse time (so a future Python
 * version adding new keys doesn't break us); unknown @c source.type
 * values round-trip as @c LOCKFILE_SRC_UNKNOWN.
 */
#include "lockfile.h"

#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "error.h"
#include "fs.h"
#include "sbuf.h"
#include "yaml.h"

static char *dup_or_null(const char *s) { return s ? sbuf_strdup(s) : NULL; }

static enum lockfile_source_type src_type_from_str(const char *s)
{
	if (!s)
		return LOCKFILE_SRC_UNKNOWN;
	if (!strcmp(s, "idf"))
		return LOCKFILE_SRC_IDF;
	if (!strcmp(s, "service"))
		return LOCKFILE_SRC_SERVICE;
	if (!strcmp(s, "git"))
		return LOCKFILE_SRC_GIT;
	if (!strcmp(s, "local"))
		return LOCKFILE_SRC_LOCAL;
	if (!strcmp(s, "path"))
		return LOCKFILE_SRC_LOCAL;
	return LOCKFILE_SRC_UNKNOWN;
}

static const char *src_type_to_str(enum lockfile_source_type t)
{
	switch (t) {
	case LOCKFILE_SRC_IDF:
		return "idf";
	case LOCKFILE_SRC_SERVICE:
		return "service";
	case LOCKFILE_SRC_GIT:
		return "git";
	case LOCKFILE_SRC_LOCAL:
		return "local";
	case LOCKFILE_SRC_UNKNOWN:
		return "unknown";
	}
	return "unknown";
}

static int parse_nested_dep(struct lockfile_nested_dep *nd,
			    struct yaml_value *m)
{
	struct yaml_value *x;

	if (yaml_type(m) != YAML_MAP)
		return -1;

	x = yaml_get(m, "name");
	if (x && yaml_type(x) == YAML_STRING)
		nd->name = dup_or_null(yaml_as_string(x));

	x = yaml_get(m, "version");
	if (x && yaml_type(x) == YAML_STRING)
		nd->version = dup_or_null(yaml_as_string(x));

	x = yaml_get(m, "require");
	if (x && yaml_type(x) == YAML_STRING)
		nd->require = dup_or_null(yaml_as_string(x));

	return 0;
}

static int parse_entry(struct lockfile_entry *e, const char *name,
		       struct yaml_value *val)
{
	struct yaml_value *x;

	if (yaml_type(val) != YAML_MAP)
		return -1;

	e->name = dup_or_null(name);

	x = yaml_get(val, "version");
	if (x && yaml_type(x) == YAML_STRING)
		e->version = dup_or_null(yaml_as_string(x));

	x = yaml_get(val, "component_hash");
	if (x && yaml_type(x) == YAML_STRING)
		e->component_hash = dup_or_null(yaml_as_string(x));

	{
		struct yaml_value *src = yaml_get(val, "source");
		if (src && yaml_type(src) == YAML_MAP) {
			x = yaml_get(src, "type");
			if (x && yaml_type(x) == YAML_STRING)
				e->src_type =
				    src_type_from_str(yaml_as_string(x));

			x = yaml_get(src, "registry_url");
			if (x && yaml_type(x) == YAML_STRING)
				e->registry_url =
				    dup_or_null(yaml_as_string(x));
			x = yaml_get(src, "git_url");
			if (x && yaml_type(x) == YAML_STRING)
				e->git_url = dup_or_null(yaml_as_string(x));
			x = yaml_get(src, "git_ref");
			if (x && yaml_type(x) == YAML_STRING)
				e->git_ref = dup_or_null(yaml_as_string(x));
			x = yaml_get(src, "path");
			if (x && yaml_type(x) == YAML_STRING)
				e->path = dup_or_null(yaml_as_string(x));
		}
	}

	x = yaml_get(val, "dependencies");
	if (x && yaml_type(x) == YAML_SEQ) {
		int n = yaml_seq_size(x);
		if (n > 0) {
			e->nested = calloc((size_t)n, sizeof(*e->nested));
			if (!e->nested)
				die_errno("calloc");
			e->nested_nr = (size_t)n;
			for (int i = 0; i < n; i++) {
				if (parse_nested_dep(&e->nested[i],
						     yaml_seq_at(x, i)) < 0)
					return -1;
			}
		}
	}

	return 0;
}

int lockfile_parse(struct lockfile *out, const char *text, size_t len)
{
	struct lockfile lf = LOCKFILE_INIT;
	struct yaml_value *root;
	struct yaml_value *x;

	root = yaml_parse(text, len);
	if (!root)
		return -1;
	if (yaml_type(root) != YAML_MAP) {
		yaml_free(root);
		return -1;
	}

	x = yaml_get(root, "version");
	if (x && yaml_type(x) == YAML_STRING)
		lf.lock_version = dup_or_null(yaml_as_string(x));

	x = yaml_get(root, "target");
	if (x && yaml_type(x) == YAML_STRING)
		lf.target = dup_or_null(yaml_as_string(x));

	x = yaml_get(root, "manifest_hash");
	if (x && yaml_type(x) == YAML_STRING)
		lf.manifest_hash = dup_or_null(yaml_as_string(x));

	x = yaml_get(root, "direct_dependencies");
	if (x && yaml_type(x) == YAML_SEQ) {
		int n = yaml_seq_size(x);
		if (n > 0) {
			lf.direct_dependencies =
			    calloc((size_t)n, sizeof(*lf.direct_dependencies));
			if (!lf.direct_dependencies)
				die_errno("calloc");
			for (int i = 0; i < n; i++) {
				struct yaml_value *it = yaml_seq_at(x, i);
				if (yaml_type(it) == YAML_STRING)
					lf.direct_dependencies
					    [lf.direct_dependencies_nr++] =
					    dup_or_null(yaml_as_string(it));
			}
		}
	}

	x = yaml_get(root, "dependencies");
	if (x && yaml_type(x) == YAML_MAP) {
		int n = x->u.map.nr;
		if (n > 0) {
			lf.entries = calloc((size_t)n, sizeof(*lf.entries));
			if (!lf.entries)
				die_errno("calloc");
			lf.nr = (size_t)n;
			for (int i = 0; i < n; i++) {
				if (parse_entry(
					&lf.entries[i], x->u.map.members[i].key,
					x->u.map.members[i].value) < 0) {
					lockfile_release(&lf);
					yaml_free(root);
					return -1;
				}
			}
		}
	}

	yaml_free(root);
	*out = lf;
	return 0;
}

int lockfile_load(struct lockfile *out, const char *path)
{
	struct sbuf sb = SBUF_INIT;
	int r;

	if (sbuf_read_file(&sb, path) < 0) {
		sbuf_release(&sb);
		return -1;
	}
	r = lockfile_parse(out, sb.buf, sb.len);
	sbuf_release(&sb);
	return r;
}

/* -------------------------------------------------------------------- */
/* Writer                                                                */
/* -------------------------------------------------------------------- */

/*
 * Scalar emission: the YAML-safe form for the values we write is
 * always double-quoted with @c " and @c \ escaped.  Matches the
 * Python tool's default only for strings ruamel decides to quote; for
 * clean alphanumerics Python leaves them unquoted.  Our choice is
 * consistent and unambiguous -- byte-identity with Python is not a
 * goal here (see lockfile.h for rationale).
 */
static void emit_qstr(struct sbuf *out, const char *s)
{
	sbuf_addch(out, '"');
	for (const char *p = s ? s : ""; *p; p++) {
		if (*p == '\\' || *p == '"')
			sbuf_addch(out, '\\');
		sbuf_addch(out, (unsigned char)*p);
	}
	sbuf_addch(out, '"');
}

static void emit_indent(struct sbuf *out, int n)
{
	for (int i = 0; i < n; i++)
		sbuf_addch(out, ' ');
}

static void emit_kv(struct sbuf *out, int indent, const char *key,
		    const char *val)
{
	if (!val)
		return;
	emit_indent(out, indent);
	sbuf_addstr(out, key);
	sbuf_addstr(out, ": ");
	emit_qstr(out, val);
	sbuf_addch(out, '\n');
}

static int cmp_entry_name(const void *a, const void *b)
{
	const struct lockfile_entry *ea = a;
	const struct lockfile_entry *eb = b;
	return strcmp(ea->name ? ea->name : "", eb->name ? eb->name : "");
}

static int cmp_cstr(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void emit_nested(struct sbuf *out, int indent,
			const struct lockfile_nested_dep *n)
{
	/* Compact block-map-in-sequence: "- name: ...\n  version: ...\n  ..."
	 * First key follows "- "; subsequent keys align at `indent + 2`. */
	emit_indent(out, indent);
	sbuf_addstr(out, "- name: ");
	emit_qstr(out, n->name ? n->name : "");
	sbuf_addch(out, '\n');
	emit_kv(out, indent + 2, "require", n->require);
	emit_kv(out, indent + 2, "version", n->version);
}

static void emit_entry(struct sbuf *out, const struct lockfile_entry *e)
{
	emit_indent(out, 2);
	sbuf_addstr(out, e->name ? e->name : "");
	sbuf_addstr(out, ":\n");

	emit_kv(out, 4, "component_hash", e->component_hash);

	if (e->nested_nr) {
		emit_indent(out, 4);
		sbuf_addstr(out, "dependencies:\n");
		for (size_t i = 0; i < e->nested_nr; i++)
			emit_nested(out, 4, &e->nested[i]);
	}

	emit_indent(out, 4);
	sbuf_addstr(out, "source:\n");
	emit_kv(out, 6, "git_ref", e->git_ref);
	emit_kv(out, 6, "git_url", e->git_url);
	emit_kv(out, 6, "path", e->path);
	emit_kv(out, 6, "registry_url", e->registry_url);
	emit_kv(out, 6, "type", src_type_to_str(e->src_type));

	emit_kv(out, 4, "version", e->version);
}

int lockfile_save(const struct lockfile *lf, const char *path)
{
	struct sbuf out = SBUF_INIT;
	struct lockfile_entry *sorted_entries = NULL;
	char **sorted_direct = NULL;
	int rc;

	/* Sort entries and direct_dependencies for deterministic output. */
	if (lf->nr) {
		sorted_entries = malloc(lf->nr * sizeof(*sorted_entries));
		if (!sorted_entries)
			die_errno("malloc");
		memcpy(sorted_entries, lf->entries,
		       lf->nr * sizeof(*sorted_entries));
		qsort(sorted_entries, lf->nr, sizeof(*sorted_entries),
		      cmp_entry_name);
	}
	if (lf->direct_dependencies_nr) {
		sorted_direct =
		    malloc(lf->direct_dependencies_nr * sizeof(*sorted_direct));
		if (!sorted_direct)
			die_errno("malloc");
		memcpy(sorted_direct, lf->direct_dependencies,
		       lf->direct_dependencies_nr * sizeof(*sorted_direct));
		qsort(sorted_direct, lf->direct_dependencies_nr,
		      sizeof(*sorted_direct), cmp_cstr);
	}

	/*
	 * Top-level keys in alphabetical order: dependencies,
	 * direct_dependencies, manifest_hash, target, version.
	 */
	if (lf->nr) {
		sbuf_addstr(&out, "dependencies:\n");
		for (size_t i = 0; i < lf->nr; i++)
			emit_entry(&out, &sorted_entries[i]);
	}
	if (lf->direct_dependencies_nr) {
		sbuf_addstr(&out, "direct_dependencies:\n");
		for (size_t i = 0; i < lf->direct_dependencies_nr; i++) {
			sbuf_addstr(&out, "- ");
			emit_qstr(&out, sorted_direct[i]);
			sbuf_addch(&out, '\n');
		}
	}
	emit_kv(&out, 0, "manifest_hash", lf->manifest_hash);
	emit_kv(&out, 0, "target", lf->target);
	emit_kv(&out, 0, "version",
		lf->lock_version ? lf->lock_version : "3.0.0");

	rc = write_file_atomic(path, out.buf, out.len);

	free(sorted_entries);
	free(sorted_direct);
	sbuf_release(&out);
	return rc;
}

void lockfile_release(struct lockfile *lf)
{
	if (!lf)
		return;

	free(lf->lock_version);
	free(lf->target);
	free(lf->manifest_hash);

	for (size_t i = 0; i < lf->direct_dependencies_nr; i++)
		free(lf->direct_dependencies[i]);
	free(lf->direct_dependencies);

	for (size_t i = 0; i < lf->nr; i++) {
		struct lockfile_entry *e = &lf->entries[i];
		free(e->name);
		free(e->version);
		free(e->registry_url);
		free(e->git_url);
		free(e->git_ref);
		free(e->path);
		free(e->component_hash);
		for (size_t j = 0; j < e->nested_nr; j++) {
			free(e->nested[j].name);
			free(e->nested[j].version);
			free(e->nested[j].require);
		}
		free(e->nested);
	}
	free(lf->entries);

	memset(lf, 0, sizeof(*lf));
}
