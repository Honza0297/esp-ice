/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file wconv.h
 * @brief UTF-8 <-> wide-char string conversion helpers (Windows).
 *
 * Thin wrappers around MultiByteToWideChar / WideCharToMultiByte that
 * return newly allocated buffers. The caller is responsible for freeing
 * the returned pointer with free().
 */
#ifndef WCONV_H
#define WCONV_H

#include <wchar.h>

/**
 * @brief Convert a UTF-8 multibyte string to a wide-char string.
 *
 * @param mbs  NUL-terminated UTF-8 source string.
 * @return Newly allocated wide-char string, or NULL on error.
 *         The caller must free() the returned pointer.
 */
wchar_t *mbs_to_wcs(const char *mbs);

/**
 * @brief Convert a UTF-8 multibyte string of known length to wide-char.
 *
 * @param mbs  UTF-8 source string (not necessarily NUL-terminated).
 * @param len  Number of bytes to convert.
 * @return Newly allocated NUL-terminated wide-char string, or NULL on error.
 *         The caller must free() the returned pointer.
 */
wchar_t *mbs_to_wcs_n(const char *mbs, size_t len);

/**
 * @brief Convert a wide-char string to a UTF-8 multibyte string.
 *
 * @param wcs  NUL-terminated wide-char source string.
 * @return Newly allocated UTF-8 string, or NULL on error.
 *         The caller must free() the returned pointer.
 */
char *wcs_to_mbs(const wchar_t *wcs);

#endif /* WCONV_H */
