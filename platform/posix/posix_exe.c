/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file platform/posix/posix_exe.c
 * @brief Resolve the absolute path of the running executable (POSIX).
 *
 * Linux:  readlink("/proc/self/exe")
 * macOS:  _NSGetExecutablePath()
 */

/* readlink() requires _POSIX_C_SOURCE >= 200112L under -std=c99. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "../../ice.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

const char *get_executable_path(const char *argv0)
{
	static char buf[4096];

#ifdef __APPLE__
	uint32_t size = (uint32_t)sizeof(buf);
	if (_NSGetExecutablePath(buf, &size) == 0)
		return buf;
#else
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		return buf;
	}
#endif

	return argv0;
}
