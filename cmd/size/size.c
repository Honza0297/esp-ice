/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file size.c
 * @brief Memory usage analysis from linker map files.
 *
 * Parses a GCC/LD linker map file and builds a memory map that shows
 * how output sections are distributed across the chip's memory types
 * (IRAM, DRAM, Flash, etc.) with size and usage totals.
 *
 * The memory map is the core representation from which all size
 * reporting (summary, detail, diff) is derived.
 *
 * Algorithm overview:
 *   1. Load chip memory ranges (hardcoded per-chip tables).
 *   2. Split linker memory regions across chip memory type boundaries,
 *      since one linker region (e.g. iram0_0_seg) may span multiple
 *      chip types (e.g. IRAM + DIRAM on esp32s3).
 *   3. Filter output sections (skip empty, NOLOAD, debug sections).
 *   4. Build the memory map: compute available size per type from
 *      regions (with DIRAM alias detection), then assign sections.
 */
#include "../../ice.h"
#include "chip.h"

/* ---- helpers -------------------------------------------------------- */

static int has_suffix(const char *s, size_t len, const char *suffix)
{
	size_t suf_len = strlen(suffix);
	return len >= suf_len && !memcmp(s + len - suf_len, suffix, suf_len);
}

/* ---- region splitting ----------------------------------------------- */

/**
 * A memory region after splitting and assignment to a chip memory type.
 */
struct split_region {
	const char *name;                  /**< Region name from map file. */
	uint64_t origin;                   /**< Start address of this fragment. */
	uint64_t length;                   /**< Size of this fragment. */
	const char *attrs;                 /**< Attributes string. */
	const struct chip_mem_range *type; /**< Assigned chip memory type. */
};

/**
 * Split map file memory regions across chip memory type boundaries.
 *
 * A single linker region (e.g. iram0_0_seg on esp32s3, covering
 * 0x40370000..0x403EFFFF) may span IRAM (0x40370000, 0x8000) and
 * DIRAM (secondary 0x40378000, 0x68000).  This function creates
 * separate fragments for each chip type.
 *
 * Also handles the edge case of zero-size reserved regions whose
 * origin sits exactly at the end of a type range (e.g.
 * rtc_fast_reserved_seg on esp32).
 */
static int split_regions(const struct map_file *mf,
			 const struct chip_info *chip,
			 struct split_region **out)
{
	struct split_region *sr = NULL;
	int nr = 0, alloc = 0;

	for (int i = 0; i < mf->nr_regions; i++) {
		struct map_region *reg = &mf->regions[i];
		uint64_t origin = reg->origin;
		uint64_t remaining = reg->length;

		if (!strcmp(reg->name, "*default*"))
			continue;

		while (remaining) {
			int found = 0;

			for (int j = 0; chip->ranges[j].name; j++) {
				const struct chip_mem_range *cr = &chip->ranges[j];
				uint64_t addr = cr->primary_addr;
				uint64_t len = cr->length;

				if (addr <= origin && origin < addr + len) {
					found = 1;
				} else if (cr->secondary_addr) {
					addr = cr->secondary_addr;
					if (addr <= origin && origin < addr + len)
						found = 1;
				}
				if (!found)
					continue;

				uint64_t avail = len - (origin - addr);
				uint64_t used = remaining < avail
					? remaining : avail;

				ALLOC_GROW(sr, nr + 1, alloc);
				sr[nr].name = reg->name;
				sr[nr].origin = origin;
				sr[nr].length = used;
				sr[nr].attrs = reg->attrs;
				sr[nr].type = cr;
				nr++;

				origin += used;
				remaining -= used;
				break;
			}

			if (!found) {
				/*
				 * Fallback for reserved regions whose origin
				 * is at the exact end of a type range.
				 */
				for (int k = 0; k < nr; k++) {
					uint64_t end = sr[k].origin + sr[k].length;
					if (origin + remaining == end) {
						ALLOC_GROW(sr, nr + 1, alloc);
						sr[nr].name = reg->name;
						sr[nr].origin = origin;
						sr[nr].length = remaining;
						sr[nr].attrs = reg->attrs;
						sr[nr].type = sr[k].type;
						nr++;
						remaining = 0;
						found = 1;
						break;
					}
				}
				if (!found)
					warn("cannot assign region '%s' to "
					     "any memory type", reg->name);
				break;
			}
		}
	}

	*out = sr;
	return nr;
}

static int region_cmp(const void *a, const void *b)
{
	const struct split_region *ra = a;
	const struct split_region *rb = b;
	if (ra->origin < rb->origin)
		return -1;
	if (ra->origin > rb->origin)
		return 1;
	return 0;
}

/* ---- section filtering ---------------------------------------------- */

/**
 * Check whether a map section should appear in the memory map.
 *
 * Without ELF section headers we filter by output section name:
 * exclude empty and NOLOAD sections, include sections whose names
 * match known patterns (.text, .data, .bss, .rodata, .flash, etc.).
 */
static int section_included(const struct map_section *sec)
{
	const char *name = sec->name;
	size_t len = strlen(name);

	if (!sec->size)
		return 0;

	/* NOLOAD dummy/reserved sections for overlapping memory regions. */
	if (has_suffix(name, len, "dummy"))
		return 0;
	if (has_suffix(name, len, "reserved_for_iram"))
		return 0;
	if (has_suffix(name, len, "noload"))
		return 0;

	/* Include sections matching known allocatable patterns. */
	return has_suffix(name, len, ".text") ||
	       has_suffix(name, len, ".data") ||
	       has_suffix(name, len, ".bss") ||
	       has_suffix(name, len, ".rodata") ||
	       has_suffix(name, len, "noinit") ||
	       has_suffix(name, len, ".vectors") ||
	       strstr(name, ".flash") != NULL ||
	       strstr(name, ".eh_frame") != NULL;
}

/* ---- memory map ----------------------------------------------------- */

struct memmap_section {
	const char *name;
	uint64_t size;
};

struct memmap_entry {
	const char *name;
	uint64_t size;
	uint64_t used;
	struct memmap_section *sections;
	int nr_sections;
	int alloc_sections;
};

struct memmap {
	const char *target;
	struct memmap_entry *entries;
	int nr_entries;
};

static struct memmap_entry *memmap_find(struct memmap *mm, const char *name)
{
	for (int i = 0; i < mm->nr_entries; i++)
		if (!strcmp(mm->entries[i].name, name))
			return &mm->entries[i];
	return NULL;
}

/**
 * Calculate available size for each memory type from split regions.
 *
 * Detects aliased regions (DIRAM) to avoid double-counting physical
 * memory.  Two regions are aliases when they map to the same memory
 * type, their mutual offset equals the offset between the type's
 * primary and secondary addresses, and their lengths match.
 */
static void compute_sizes(struct memmap *mm,
			  const struct split_region *regions, int nr)
{
	for (int i = 0; i < nr; i++) {
		const struct split_region *sr = &regions[i];
		struct memmap_entry *e = memmap_find(mm, sr->type->name);

		/* Offset between primary and secondary addresses. */
		uint64_t type_offset = 0;
		if (sr->type->secondary_addr) {
			uint64_t p = sr->type->primary_addr;
			uint64_t s = sr->type->secondary_addr;
			type_offset = p > s ? p - s : s - p;
		}

		/* Check against previously counted regions. */
		int is_alias = 0;
		for (int k = 0; k < i; k++) {
			if (strcmp(regions[k].type->name, sr->type->name))
				continue;
			uint64_t a = regions[k].origin;
			uint64_t b = sr->origin;
			uint64_t offset = a > b ? a - b : b - a;
			if (type_offset == offset &&
			    regions[k].length == sr->length) {
				is_alias = 1;
				break;
			}
		}

		if (!is_alias)
			e->size += sr->length;
	}
}

/**
 * Assign output sections to memory types based on address containment.
 *
 * Sections that span multiple split regions (i.e. cross a memory type
 * boundary) are split: each portion is accounted to the correct type.
 * For example on esp32s3, .iram0.text may start in IRAM and overflow
 * into DIRAM; each part gets its own memmap_section entry.
 */
static void assign_sections(struct memmap *mm, const struct map_file *mf,
			    const struct split_region *regions, int nr_regions)
{
	for (int i = 0; i < mf->nr_sections; i++) {
		struct map_section *sec = &mf->sections[i];
		uint64_t addr = sec->address;
		uint64_t remaining = sec->size;

		if (!section_included(sec))
			continue;

		while (remaining) {
			int found = 0;
			for (int j = 0; j < nr_regions; j++) {
				uint64_t rstart = regions[j].origin;
				uint64_t rend = rstart + regions[j].length;
				struct memmap_entry *e;
				uint64_t chunk;

				if (addr < rstart || addr >= rend)
					continue;

				chunk = remaining;
				if (addr + chunk > rend)
					chunk = rend - addr;

				e = memmap_find(mm, regions[j].type->name);
				e->used += chunk;

				ALLOC_GROW(e->sections, e->nr_sections + 1,
					   e->alloc_sections);
				e->sections[e->nr_sections].name = sec->name;
				e->sections[e->nr_sections].size = chunk;
				e->nr_sections++;

				addr += chunk;
				remaining -= chunk;
				found = 1;
				break;
			}

			if (!found) {
				warn("cannot assign section '%s' to "
				     "any memory type", sec->name);
				break;
			}
		}
	}
}

static void memmap_build(struct memmap *mm, const char *target,
			 struct map_file *mf)
{
	const struct chip_info *chip = chip_find(target);
	if (!chip)
		die("unknown target '%s'", target);

	mm->target = target;

	/* Count ranges and pre-create one entry per unique display name. */
	int nr_ranges = 0;
	while (chip->ranges[nr_ranges].name)
		nr_ranges++;

	mm->entries = calloc(nr_ranges, sizeof(*mm->entries));
	if (!mm->entries)
		die_errno("calloc");
	mm->nr_entries = 0;

	for (int i = 0; i < nr_ranges; i++) {
		if (!memmap_find(mm, chip->ranges[i].name)) {
			mm->entries[mm->nr_entries].name =
				chip->ranges[i].name;
			mm->nr_entries++;
		}
	}

	/* Split map regions across memory type boundaries. */
	struct split_region *regions;
	int nr_regions = split_regions(mf, chip, &regions);

	/* Sort for deterministic section-to-region assignment. */
	qsort(regions, nr_regions, sizeof(regions[0]), region_cmp);

	/* Populate sizes and assign sections. */
	compute_sizes(mm, regions, nr_regions);
	assign_sections(mm, mf, regions, nr_regions);

	free(regions);
}

static void memmap_dump(const struct memmap *mm)
{
	for (int i = 0; i < mm->nr_entries; i++) {
		const struct memmap_entry *e = &mm->entries[i];
		if (!e->size && !e->used)
			continue;
		printf("%-14s %llu / %llu\n", e->name,
		       (unsigned long long)e->used,
		       (unsigned long long)e->size);
		for (int j = 0; j < e->nr_sections; j++)
			printf("  %-30s %llu\n",
			       e->sections[j].name,
			       (unsigned long long)e->sections[j].size);
	}
}

static void memmap_release(struct memmap *mm)
{
	for (int i = 0; i < mm->nr_entries; i++)
		free(mm->entries[i].sections);
	free(mm->entries);
}

/* ---- command -------------------------------------------------------- */

int cmd_size(int argc, const char **argv)
{
	const char *target = NULL;

	struct option opts[] = {
		OPT_STRING('t', "target", &target, "chip",
			   "target chip (e.g. esp32s3)"),
		OPT_END(),
	};
	const char *usage[] = {
		"ice size --target <chip> <map-file>",
		NULL,
	};

	argc = parse_options(argc, argv, opts, usage);
	if (!target)
		die("--target is required; see 'ice size --help'");
	if (argc < 1)
		die("no map file; see 'ice size --help'");

	struct sbuf sb = SBUF_INIT;
	if (sbuf_read_file(&sb, argv[0]) < 0)
		die_errno("cannot read '%s'", argv[0]);

	struct map_file mf;
	map_read(sb.buf, sb.len, &mf);

	struct memmap mm;
	memmap_build(&mm, target, &mf);
	memmap_dump(&mm);

	memmap_release(&mm);
	map_release(&mf);
	sbuf_release(&sb);

	return 0;
}
