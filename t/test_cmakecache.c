/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for cmakecache.c -- the parsed CMakeCache.txt reader.
 */
#include "ice.h"
#include "tap.h"

#include <unistd.h>

/* Write a fixture cache file at @p path; abort the run on any error. */
static void write_fixture(const char *path, const char *content)
{
	FILE *fp = fopen(path, "wb");

	if (!fp || fputs(content, fp) == EOF || fclose(fp) != 0)
		die_errno("write fixture: %s", path);
}

int main(void)
{
	/* CMAKECACHE_INIT yields an empty store. */
	{
		struct cmakecache c = CMAKECACHE_INIT;
		tap_check(c.entries == NULL);
		tap_check(c.nr == 0);
		cmakecache_release(&c);
		tap_done("CMAKECACHE_INIT yields empty store");
	}

	/* Parse a small fixture covering comments, blanks, and KEY:TYPE=VALUE.
	 */
	{
		struct cmakecache c = CMAKECACHE_INIT;
		const char *path = "cache.txt";

		write_fixture(path, "# top-level comment\n"
				    "// alternative comment style\n"
				    "\n"
				    "IDF_TARGET:STRING=esp32s3\n"
				    "PROJECT_NAME:STRING=hello-world\n"
				    "FLAG_BOOL:BOOL=ON\n");

		tap_check(cmakecache_load(&c, path) == 0);
		tap_check(c.nr == 3);
		tap_check(strcmp(cmakecache_get(&c, "IDF_TARGET"), "esp32s3") ==
			  0);
		tap_check(strcmp(cmakecache_get(&c, "PROJECT_NAME"),
				 "hello-world") == 0);
		tap_check(strcmp(cmakecache_get(&c, "FLAG_BOOL"), "ON") == 0);

		cmakecache_release(&c);
		unlink(path);
		tap_done("parse fixture; lookups return the value side of "
			 "KEY:TYPE=VALUE");
	}

	/* Missing key returns NULL. */
	{
		struct cmakecache c = CMAKECACHE_INIT;
		const char *path = "cache.txt";

		write_fixture(path, "X:STRING=y\n");
		cmakecache_load(&c, path);
		tap_check(cmakecache_get(&c, "MISSING") == NULL);
		cmakecache_release(&c);
		unlink(path);
		tap_done("missing key returns NULL");
	}

	/* I/O error: missing file returns -1. */
	{
		struct cmakecache c = CMAKECACHE_INIT;
		tap_check(cmakecache_load(&c, "no_such_file") == -1);
		cmakecache_release(&c);
		tap_done("missing file returns -1");
	}

	return tap_result();
}
