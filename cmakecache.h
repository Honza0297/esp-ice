/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmakecache.h
 * @brief Minimal reader for CMakeCache.txt files.
 *
 * Cache lines have the form "KEY:TYPE=VALUE"; blank lines and lines
 * starting with '#' or '//' are comments.  This module does not
 * write or modify caches; it only looks up values.
 */
#ifndef CMAKECACHE_H
#define CMAKECACHE_H

/**
 * @brief Look up @p key in a CMakeCache.txt buffer.
 *
 * @param buf  NUL-terminated cache contents.
 * @param key  Entry name (without ":TYPE" suffix).
 * @return     Pointer into @p buf at the start of the value, or NULL
 *             if @p key is not present.  The value runs up to the next
 *             '\\n' or end-of-buffer; it is NOT NUL-terminated at the
 *             end of the value.
 */
const char *cmakecache_lookup(const char *buf, const char *key);

/**
 * @brief Heap-allocated NUL-terminated copy of @p key's value.
 *
 * Thin wrapper over cmakecache_lookup() for callers that just want
 * the string.  Returns NULL if the key is not present.  Caller frees.
 */
char *cmakecache_get(const char *buf, const char *key);

#endif /* CMAKECACHE_H */
