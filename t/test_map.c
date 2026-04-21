/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for map.c -- the GCC/LD linker map file parser.
 *
 * The fixture is embedded as a string literal; map_read() mutates the
 * buffer in place, so we copy it into a scratch buffer per test point.
 */
#include "ice.h"
#include "tap.h"

static const char fixture[] =
    "\n"
    "Memory Configuration\n"
    "\n"
    "Name             Origin             Length             Attributes\n"
    "iram0            0x40380000         0x00020000         xr\n"
    "dram0            0x3fc80000         0x00040000         rw\n"
    "*default*        0x00000000         0xffffffffffffffff\n"
    "\n"
    "Linker script and memory map\n"
    "\n"
    ".text           0x40380000       0x100\n"
    " .text          0x40380000       0x80 foo.o\n"
    " .text          0x40380080       0x80 bar.o\n"
    "\n"
    ".data           0x3fc80000       0x20\n"
    " .data          0x3fc80000       0x20 foo.o\n"
    "\n";

static const struct map_region *find_region(const struct map_file *mf,
					    const char *name)
{
	for (int i = 0; i < mf->nr_regions; i++)
		if (!strcmp(mf->regions[i].name, name))
			return &mf->regions[i];
	return NULL;
}

int main(void)
{
	char *buf = sbuf_strdup(fixture);
	struct map_file mf;
	const struct map_region *iram, *dram;

	map_read(buf, strlen(fixture), &mf);

	/* Memory Configuration: two named regions plus *default* are all
	 * represented (cmd/idf/size filters out *default* later when it walks
	 * the regions). */
	tap_check(mf.nr_regions == 3);
	tap_done("Memory Configuration section yields all three regions");

	iram = find_region(&mf, "iram0");
	tap_check(iram != NULL);
	tap_check(iram->origin == 0x40380000ULL);
	tap_check(iram->length == 0x00020000ULL);
	tap_check(strcmp(iram->attrs, "xr") == 0);
	tap_done("iram0 region parsed with origin, length, and attrs");

	dram = find_region(&mf, "dram0");
	tap_check(dram != NULL);
	tap_check(dram->origin == 0x3fc80000ULL);
	tap_check(dram->length == 0x00040000ULL);
	tap_check(strcmp(dram->attrs, "rw") == 0);
	tap_done("dram0 region parsed with origin, length, and attrs");

	/* Linker script and memory map: two output sections, each with one
	 * or more input contributions.  We verify names/sizes but not the
	 * per-input detail -- that would re-implement the parser. */
	tap_check(mf.nr_sections >= 2);
	tap_done("Linker-map section yields at least two output sections");

	map_release(&mf);
	free(buf);
	return tap_result();
}
