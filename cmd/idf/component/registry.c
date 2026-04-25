/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ESP Component Registry metadata client.  Discovers the storage URL
 * once per registry, then fetches per-component JSON metadata and
 * normalises it into the shape the solver consumes.  See registry.h.
 */
#include "registry.h"

#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "error.h"
#include "http.h"
#include "json.h"
#include "sbuf.h"

static char *dup_or_null(const char *s) { return s ? sbuf_strdup(s) : NULL; }

static void ensure_trailing_slash(struct sbuf *url)
{
	if (url->len == 0 || url->buf[url->len - 1] != '/')
		sbuf_addch(url, '/');
}

/* -------------------------------------------------------------------- */
/* /api/ -> storage URL discovery (cached)                               */
/* -------------------------------------------------------------------- */

struct storage_cache_entry {
	char *registry_url; /* exact key we were called with */
	char *storage_url;  /* normalised, with trailing slash */
};

static struct storage_cache_entry *storage_cache;
static size_t storage_cache_nr;
static size_t storage_cache_alloc;

const char *reg_storage_url(const char *registry_url)
{
	struct sbuf url = SBUF_INIT;
	struct sbuf body = SBUF_INIT;
	struct sbuf storage = SBUF_INIT;
	struct json_value *root = NULL;
	const char *base;
	const char *result = NULL;

	if (!registry_url || !*registry_url)
		return NULL;

	for (size_t i = 0; i < storage_cache_nr; i++) {
		if (!strcmp(storage_cache[i].registry_url, registry_url))
			return storage_cache[i].storage_url;
	}

	sbuf_addstr(&url, registry_url);
	ensure_trailing_slash(&url);
	sbuf_addstr(&url, "api/");

	if (http_get(url.buf, &body) != 200)
		goto out;

	root = json_parse(body.buf, body.len);
	if (!root)
		goto out;

	base = json_as_string(json_get(root, "components_base_url"));
	if (!base)
		goto out;

	sbuf_addstr(&storage, base);
	ensure_trailing_slash(&storage);

	ALLOC_GROW(storage_cache, storage_cache_nr + 1, storage_cache_alloc);
	storage_cache[storage_cache_nr].registry_url =
	    sbuf_strdup(registry_url);
	storage_cache[storage_cache_nr].storage_url = sbuf_detach(&storage);
	result = storage_cache[storage_cache_nr].storage_url;
	storage_cache_nr++;

out:
	if (root)
		json_free(root);
	sbuf_release(&url);
	sbuf_release(&body);
	sbuf_release(&storage);
	return result;
}

/* -------------------------------------------------------------------- */
/* Per-component metadata fetch                                          */
/* -------------------------------------------------------------------- */

/* Convert one entry of the JSON @c dependencies array into our shape.
 * The registry's representation:
 *
 *   { "spec": ">=5.0", "source": "idf",
 *     "name": null, "namespace": null,
 *     "registry_url": null, "rules": [], ... }
 *
 *   { "spec": "^1.0.0", "source": "service",
 *     "name": "usb", "namespace": "espressif",
 *     "registry_url": "https://components.espressif.com",
 *     "rules": [{"if": "..."}, ...] }
 */
static int parse_dep(struct reg_dep *out, const struct json_value *jd,
		     const char *default_registry_url)
{
	const char *src = json_as_string(json_get(jd, "source"));
	const char *spec = json_as_string(json_get(jd, "spec"));

	if (!spec)
		spec = "*";

	memset(out, 0, sizeof(*out));
	out->spec = sbuf_strdup(spec);

	if (src && !strcmp(src, "idf")) {
		out->is_idf = 1;
		out->name = sbuf_strdup("idf");
	} else {
		const char *ns = json_as_string(json_get(jd, "namespace"));
		const char *nm = json_as_string(json_get(jd, "name"));
		const char *reg = json_as_string(json_get(jd, "registry_url"));
		struct sbuf full = SBUF_INIT;

		if (!nm) {
			free(out->spec);
			return -1;
		}
		if (ns && *ns)
			sbuf_addf(&full, "%s/%s", ns, nm);
		else
			sbuf_addstr(&full, nm);
		out->name = sbuf_detach(&full);
		out->registry_url =
		    dup_or_null(reg ? reg : default_registry_url);
	}

	out->if_expr = dup_or_null(json_as_string(json_get(jd, "if")));

	{
		const struct json_value *rules = json_get(jd, "rules");
		int n = json_array_size(rules);
		if (n > 0) {
			out->rules_if =
			    calloc((size_t)n, sizeof(*out->rules_if));
			if (!out->rules_if)
				die_errno("calloc");
			out->rules_nr = (size_t)n;
			for (int i = 0; i < n; i++) {
				const struct json_value *r =
				    json_array_at(rules, i);
				const char *e =
				    json_as_string(json_get(r, "if"));
				out->rules_if[i] = dup_or_null(e);
			}
		}
	}

	return 0;
}

static void release_dep(struct reg_dep *d)
{
	free(d->name);
	free(d->spec);
	free(d->registry_url);
	free(d->if_expr);
	for (size_t i = 0; i < d->rules_nr; i++)
		free(d->rules_if[i]);
	free(d->rules_if);
}

static int parse_version(struct reg_version *out, const struct json_value *jv,
			 const char *storage_url,
			 const char *default_registry_url)
{
	const char *ver = json_as_string(json_get(jv, "version"));
	const char *url_rel = json_as_string(json_get(jv, "url"));
	const char *hash = json_as_string(json_get(jv, "component_hash"));
	const struct json_value *jdeps = json_get(jv, "dependencies");
	int n;
	struct sbuf abs = SBUF_INIT;

	if (!ver || !url_rel)
		return -1;

	memset(out, 0, sizeof(*out));
	out->version = sbuf_strdup(ver);
	out->component_hash = dup_or_null(hash);

	sbuf_addstr(&abs, storage_url);
	sbuf_addstr(&abs, url_rel);
	out->download_url = sbuf_detach(&abs);

	n = json_array_size(jdeps);
	if (n > 0) {
		out->deps = calloc((size_t)n, sizeof(*out->deps));
		if (!out->deps)
			die_errno("calloc");
		for (int i = 0; i < n; i++) {
			if (parse_dep(&out->deps[out->deps_nr],
				      json_array_at(jdeps, i),
				      default_registry_url) == 0)
				out->deps_nr++;
		}
	}
	return 0;
}

static void release_version(struct reg_version *v)
{
	free(v->version);
	free(v->download_url);
	free(v->component_hash);
	for (size_t i = 0; i < v->deps_nr; i++)
		release_dep(&v->deps[i]);
	free(v->deps);
}

int reg_fetch_component(struct reg_component *out, const char *registry_url,
			const char *full_name)
{
	const char *storage = reg_storage_url(registry_url);
	struct sbuf url = SBUF_INIT;
	struct sbuf body = SBUF_INIT;
	struct json_value *root = NULL;
	const struct json_value *jvers;
	struct reg_component c = REG_COMPONENT_INIT;
	int n;
	int rc = -1;

	if (!storage)
		goto out;

	sbuf_addstr(&url, storage);
	sbuf_addf(&url, "components/%s.json", full_name);

	if (http_get(url.buf, &body) != 200)
		goto out;

	root = json_parse(body.buf, body.len);
	if (!root)
		goto out;

	c.name = sbuf_strdup(full_name);
	c.registry_url = sbuf_strdup(registry_url);

	jvers = json_get(root, "versions");
	n = json_array_size(jvers);
	if (n > 0) {
		c.versions = calloc((size_t)n, sizeof(*c.versions));
		if (!c.versions)
			die_errno("calloc");
		for (int i = 0; i < n; i++) {
			if (parse_version(&c.versions[c.versions_nr],
					  json_array_at(jvers, i), storage,
					  registry_url) == 0)
				c.versions_nr++;
		}
	}

	*out = c;
	rc = 0;

out:
	if (rc != 0)
		reg_release(&c);
	if (root)
		json_free(root);
	sbuf_release(&url);
	sbuf_release(&body);
	return rc;
}

void reg_release(struct reg_component *c)
{
	if (!c)
		return;
	free(c->name);
	free(c->registry_url);
	for (size_t i = 0; i < c->versions_nr; i++)
		release_version(&c->versions[i]);
	free(c->versions);
	memset(c, 0, sizeof(*c));
}
