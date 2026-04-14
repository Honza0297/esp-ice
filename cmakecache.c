/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmakecache.c
 * @brief Minimal CMakeCache.txt reader.
 */
#include "ice.h"

const char *cmakecache_lookup(const char *buf, const char *key)
{
	size_t key_len = strlen(key);
	const char *p = buf;

	while (*p) {
		if (*p == '#' || (*p == '/' && p[1] == '/') || *p == '\n') {
			p = strchr(p, '\n');
			if (!p)
				break;
			p++;
			continue;
		}

		if (!strncmp(p, key, key_len) && p[key_len] == ':') {
			const char *eq = strchr(p + key_len, '=');
			if (eq)
				return eq + 1;
		}

		p = strchr(p, '\n');
		if (!p)
			break;
		p++;
	}
	return NULL;
}

char *cmakecache_get(const char *buf, const char *key)
{
	const char *val, *nl;

	val = cmakecache_lookup(buf, key);
	if (!val)
		return NULL;

	nl = strchr(val, '\n');
	return sbuf_strndup(val, nl ? (size_t)(nl - val) : strlen(val));
}
