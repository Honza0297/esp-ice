/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmakecache.c
 * @brief Parsed CMakeCache.txt reader.
 */
#include "ice.h"

void cmakecache_init(struct cmakecache *c)
{
	c->entries = NULL;
	c->nr = 0;
	c->alloc = 0;
}

void cmakecache_release(struct cmakecache *c)
{
	for (int i = 0; i < c->nr; i++) {
		free(c->entries[i].key);
		free(c->entries[i].value);
	}
	free(c->entries);
	cmakecache_init(c);
}

int cmakecache_load(struct cmakecache *c, const char *path)
{
	struct sbuf sb = SBUF_INIT;
	size_t pos = 0;
	char *line;

	if (sbuf_read_file(&sb, path) < 0) {
		sbuf_release(&sb);
		return -1;
	}

	while ((line = sbuf_getline(sb.buf, sb.len, &pos)) != NULL) {
		char *colon, *eq;

		while (*line == ' ' || *line == '\t')
			line++;
		if (*line == '\0' || *line == '#' ||
		    (*line == '/' && line[1] == '/'))
			continue;

		colon = strchr(line, ':');
		if (!colon)
			continue;
		eq = strchr(colon, '=');
		if (!eq)
			continue;

		ALLOC_GROW(c->entries, c->nr + 1, c->alloc);
		c->entries[c->nr].key = sbuf_strndup(line, colon - line);
		c->entries[c->nr].value = sbuf_strdup(eq + 1);
		c->nr++;
	}

	sbuf_release(&sb);
	return 0;
}

const char *cmakecache_get(const struct cmakecache *c, const char *key)
{
	for (int i = 0; i < c->nr; i++) {
		if (!strcmp(c->entries[i].key, key))
			return c->entries[i].value;
	}
	return NULL;
}
