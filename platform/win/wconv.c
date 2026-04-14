/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file wconv.c
 * @brief UTF-8 <-> wide-char string conversion helpers (Windows).
 *
 * Implements the conversion routines declared in wconv.h using the Win32
 * MultiByteToWideChar / WideCharToMultiByte APIs.
 *
 * IMPORTANT: This is the only file where ice.h is NOT the first include.
 *
 * ice.h pulls in platform.h which redefines fprintf/vfprintf. Since this
 * file implements code called from those overrides, error reporting here
 * must use the real C-library functions to avoid infinite recursion. We
 * capture the original function addresses *before* including ice.h.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Original C-library fprintf, captured before platform.h overrides it. */
static int (*fprintf_raw)(FILE *, const char *, ...) = fprintf;

/** Original C-library vfprintf, captured before platform.h overrides it. */
static int (*vfprintf_raw)(FILE *, const char *, va_list) = vfprintf;

/* Now safe to pull in the project header. */
#include <stringapiset.h>
#include <wchar.h>
#include <windows.h>
#include <winerror.h>

#include "ice.h"
#include "wconv.h"

/**
 * @brief Raw error reporting that bypasses the fprintf override.
 *
 * Used only in this file to report errors during UTF-8 <-> wide-char
 * conversion without re-entering the override (which calls us).
 */
static void err_raw(const char *fmt, ...)
{
	va_list ap;
	fprintf_raw(stderr, "error: ");
	va_start(ap, fmt);
	vfprintf_raw(stderr, fmt, ap);
	va_end(ap);
	fprintf_raw(stderr, "\n");
}

/** Raw error reporting with errno. */
static void err_errno_raw(const char *fmt, ...)
{
	va_list ap;
	fprintf_raw(stderr, "error: ");
	va_start(ap, fmt);
	vfprintf_raw(stderr, fmt, ap);
	va_end(ap);
	fprintf_raw(stderr, ": %s (%d)\n", strerror(errno), errno);
}

/**
 * @brief Convert a UTF-8 multibyte string to a wide-char string.
 *
 * @param mbs  NUL-terminated UTF-8 source string.
 * @return Newly allocated wide-char string, or NULL on error.
 */
wchar_t *mbs_to_wcs(const char *mbs)
{
	int cnt;
	wchar_t *wcs;

	cnt = MultiByteToWideChar(CP_UTF8, 0, mbs, -1, NULL, 0);
	if (!cnt) {
		err_raw("MultiByteToWideChar failed (%lu)", GetLastError());
		return NULL;
	}

	wcs = malloc((size_t)cnt * sizeof(wchar_t));
	if (!wcs) {
		err_errno_raw("malloc failed");
		return NULL;
	}

	if (!MultiByteToWideChar(CP_UTF8, 0, mbs, -1, wcs, cnt)) {
		err_raw("MultiByteToWideChar failed (%lu)", GetLastError());
		free(wcs);
		return NULL;
	}

	return wcs;
}

/**
 * @brief Convert a UTF-8 multibyte string of known length to wide-char.
 *
 * @param mbs  UTF-8 source string (not necessarily NUL-terminated).
 * @param len  Number of bytes to convert.
 * @return Newly allocated NUL-terminated wide-char string, or NULL on error.
 */
wchar_t *mbs_to_wcs_n(const char *mbs, size_t len)
{
	int cnt;
	wchar_t *wcs;

	cnt = MultiByteToWideChar(CP_UTF8, 0, mbs, (int)len, NULL, 0);
	if (!cnt) {
		err_raw("MultiByteToWideChar failed (%lu)", GetLastError());
		return NULL;
	}

	wcs = malloc(((size_t)cnt + 1) * sizeof(wchar_t));
	if (!wcs) {
		err_errno_raw("malloc failed");
		return NULL;
	}

	if (!MultiByteToWideChar(CP_UTF8, 0, mbs, (int)len, wcs, cnt)) {
		err_raw("MultiByteToWideChar failed (%lu)", GetLastError());
		free(wcs);
		return NULL;
	}

	wcs[cnt] = L'\0';
	return wcs;
}

/**
 * @brief Convert a wide-char string to a UTF-8 multibyte string.
 *
 * @param wcs  NUL-terminated wide-char source string.
 * @return Newly allocated UTF-8 string, or NULL on error.
 */
char *wcs_to_mbs(const wchar_t *wcs)
{
	int cnt;
	char *mbs;

	cnt = WideCharToMultiByte(CP_UTF8, 0, wcs, -1, NULL, 0, NULL, NULL);
	if (!cnt) {
		err_raw("WideCharToMultiByte failed (%lu)", GetLastError());
		return NULL;
	}

	mbs = malloc((size_t)cnt);
	if (!mbs) {
		err_errno_raw("malloc failed");
		return NULL;
	}

	if (!WideCharToMultiByte(CP_UTF8, 0, wcs, -1, mbs, cnt, NULL, NULL)) {
		err_raw("WideCharToMultiByte failed (%lu)", GetLastError());
		free(mbs);
		return NULL;
	}

	return mbs;
}
