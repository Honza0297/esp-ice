/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * idf_component.yml parser -- a thin adapter over yaml.c.
 *
 * Each manifest is a YAML map; we copy out the fields the component
 * manager flow actually uses (version, description, targets,
 * dependencies) and drop everything else.  Deps accept both the
 * simple scalar form (``name: "spec"``) and the full block form
 * (``name: {version: ..., public: ..., git: ..., path: ...}``).
 * ``${VAR}`` expansion in path/git strings is the caller's
 * responsibility.
 */
#include "manifest.h"

#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "error.h"
#include "sbuf.h"
#include "yaml.h"

static char *dup_or_null(const char *s) { return s ? sbuf_strdup(s) : NULL; }

static int parse_dep_value(struct manifest_dep *d, struct yaml_value *v)
{
	struct yaml_value *x;

	d->is_public = -1;
	d->source = MANIFEST_SRC_REGISTRY;

	if (yaml_type(v) == YAML_STRING) {
		d->spec = dup_or_null(yaml_as_string(v));
		return 0;
	}

	if (yaml_type(v) != YAML_MAP)
		return -1;

	x = yaml_get(v, "version");
	if (x && yaml_type(x) == YAML_STRING)
		d->spec = dup_or_null(yaml_as_string(x));

	x = yaml_get(v, "public");
	if (x)
		d->is_public = yaml_as_bool(x) ? 1 : 0;

	x = yaml_get(v, "pre_release");
	if (x)
		d->pre_release = yaml_as_bool(x) ? 1 : 0;

	x = yaml_get(v, "path");
	if (x && yaml_type(x) == YAML_STRING) {
		d->source = MANIFEST_SRC_PATH;
		d->path = dup_or_null(yaml_as_string(x));
	}

	x = yaml_get(v, "git");
	if (x && yaml_type(x) == YAML_STRING) {
		d->source = MANIFEST_SRC_GIT;
		d->git_url = dup_or_null(yaml_as_string(x));
	}

	x = yaml_get(v, "service_url");
	if (x && yaml_type(x) == YAML_STRING)
		d->service_url = dup_or_null(yaml_as_string(x));

	x = yaml_get(v, "if");
	if (x && yaml_type(x) == YAML_STRING)
		d->if_expr = dup_or_null(yaml_as_string(x));

	x = yaml_get(v, "rules");
	if (x && yaml_type(x) == YAML_SEQ) {
		int n = yaml_seq_size(x);
		if (n > 0) {
			d->rules = calloc((size_t)n, sizeof(*d->rules));
			if (!d->rules)
				die_errno("calloc");
			d->rules_nr = (size_t)n;
			for (int i = 0; i < n; i++) {
				struct yaml_value *item = yaml_seq_at(x, i);
				struct yaml_value *ifv;
				if (yaml_type(item) != YAML_MAP)
					continue;
				ifv = yaml_get(item, "if");
				if (ifv && yaml_type(ifv) == YAML_STRING)
					d->rules[i].if_expr =
					    dup_or_null(yaml_as_string(ifv));
			}
		}
	}

	return 0;
}

int manifest_parse(struct manifest *out, const char *text, size_t len)
{
	struct manifest m = MANIFEST_INIT;
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
		m.version = dup_or_null(yaml_as_string(x));

	x = yaml_get(root, "description");
	if (x && yaml_type(x) == YAML_STRING)
		m.description = dup_or_null(yaml_as_string(x));

	x = yaml_get(root, "targets");
	if (x && yaml_type(x) == YAML_SEQ) {
		int n = yaml_seq_size(x);
		if (n > 0) {
			m.targets = calloc((size_t)n, sizeof(*m.targets));
			if (!m.targets)
				die_errno("calloc");
			for (int i = 0; i < n; i++) {
				struct yaml_value *item = yaml_seq_at(x, i);
				if (yaml_type(item) == YAML_STRING) {
					m.targets[m.targets_nr++] =
					    dup_or_null(yaml_as_string(item));
				}
			}
		}
	}

	x = yaml_get(root, "dependencies");
	if (x && yaml_type(x) == YAML_MAP) {
		int n = x->u.map.nr;
		if (n > 0) {
			m.deps = calloc((size_t)n, sizeof(*m.deps));
			if (!m.deps)
				die_errno("calloc");
			m.deps_nr = (size_t)n;
			for (int i = 0; i < n; i++) {
				m.deps[i].name =
				    dup_or_null(x->u.map.members[i].key);
				if (parse_dep_value(&m.deps[i],
						    x->u.map.members[i].value) <
				    0) {
					manifest_release(&m);
					yaml_free(root);
					return -1;
				}
			}
		}
	}

	yaml_free(root);
	*out = m;
	return 0;
}

int manifest_load(struct manifest *out, const char *path)
{
	struct sbuf sb = SBUF_INIT;
	int r;

	if (sbuf_read_file(&sb, path) < 0) {
		sbuf_release(&sb);
		return -1;
	}
	r = manifest_parse(out, sb.buf, sb.len);
	sbuf_release(&sb);
	return r;
}

void manifest_release(struct manifest *m)
{
	if (!m)
		return;

	free(m->version);
	free(m->description);

	for (size_t i = 0; i < m->deps_nr; i++) {
		struct manifest_dep *d = &m->deps[i];
		free(d->name);
		free(d->spec);
		free(d->git_url);
		free(d->path);
		free(d->service_url);
		free(d->if_expr);
		for (size_t j = 0; j < d->rules_nr; j++)
			free(d->rules[j].if_expr);
		free(d->rules);
	}
	free(m->deps);

	for (size_t i = 0; i < m->targets_nr; i++)
		free(m->targets[i]);
	free(m->targets);

	memset(m, 0, sizeof(*m));
}
