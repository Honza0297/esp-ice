/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file elf2image.c
 * @brief ELF → ESP flash-image conversion engine.
 *
 * Ported from esptool's @c esptool/bin_image.py
 * (@c ESP32FirmwareImage.save()) with per-chip memory tables
 * transcribed from @c esptool/targets/esp*.py.  The goal is a
 * functional, bootable image — not necessarily byte-identical to
 * @c esptool @c elf2image output, because esptool uses ELF sections
 * by default while this engine uses PT_LOAD program headers (a.k.a.
 * esptool's @c --use-segments mode).  Both forms are accepted by
 * the ROM bootloader.
 *
 * Known simplifications vs. full esptool parity:
 *   - PT_LOAD segments only (no per-ELF-section granularity).
 *   - MMU page size fixed at 64 KB (IROM_ALIGN); chips with
 *     configurable MMU page size always use 64 KB.
 *   - No bootloader-image specials (ram_only_header, secure_pad,
 *     bootdesc reordering).
 *   - No ESP8266 (different image format, V1/V2; not needed for
 *     ESP-IDF 5.x app images).
 *   - ELF SHA-256 patching requires an explicit offset; the app-desc
 *     auto-detect path is not implemented (IDF passes
 *     --elf-sha256-offset 0xb0 explicitly anyway).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "elf2image.h"
#include "ice.h"
#include "vendor/sha256/sha256.h"

/* ------------------------------------------------------------------ */
/* Image format constants                                             */
/* ------------------------------------------------------------------ */

#define E2I_IMAGE_MAGIC 0xE9u
#define E2I_CHECKSUM_MAGIC 0xEFu
#define E2I_IROM_ALIGN 0x10000u /* 64 KB, see IROM_ALIGN in esptool */
#define E2I_SEG_HDR_LEN 8u	/* load_addr(4) + data_size(4) */
#define E2I_HDR_LEN 8u		/* common header (ESP32+ and ESP8266) */
#define E2I_EXT_HDR_LEN 16u	/* extended header (ESP32+ only) */
#define E2I_DIGEST_LEN 32u	/* SHA-256 output */
#define E2I_MAX_SEGS 16u	/* ROM bootloader cap */

/* Offset (in ext header bytes) of the append_digest flag. */
#define E2I_EXT_APPEND_DIGEST_OFF 15u

/* ------------------------------------------------------------------ */
/* Per-chip tables                                                    */
/* ------------------------------------------------------------------ */

enum e2i_seg_type {
	E2I_SEG_DROM,	  /* flash-mapped read-only data */
	E2I_SEG_IROM,	  /* flash-mapped code */
	E2I_SEG_DRAM,	  /* internal RAM (data) */
	E2I_SEG_IRAM,	  /* internal RAM (code) */
	E2I_SEG_RTC_DATA, /* RTC slow/fast memory */
	E2I_SEG_UNKNOWN
};

/*
 * One contiguous virtual-address range belonging to a memory type.
 * vaddr_end is exclusive (addr < vaddr_end), matching esptool's
 * MAP_START/MAP_END convention.
 */
struct seg_range {
	uint32_t vaddr_lo;
	uint32_t vaddr_end;
	enum e2i_seg_type type;
};

/* Sentinel terminates a per-chip range table. */
#define SEG_END {0, 0, E2I_SEG_UNKNOWN}

/* ESP32
 * Source: esptool/targets/esp32.py (IROM/DROM + MEMORY_MAP). */
static const struct seg_range r_esp32[] = {
    {0x3F400000u, 0x3F800000u, E2I_SEG_DROM},
    {0x3FF80000u, 0x3FF82000u, E2I_SEG_RTC_DATA}, /* RTC_DATA */
    {0x3FFAE000u, 0x40000000u, E2I_SEG_DRAM},	  /* BYTE_ACCESSIBLE + DRAM */
    {0x40070000u, 0x40080000u, E2I_SEG_IRAM},	  /* DIRAM/IRAM cache */
    {0x40080000u, 0x400A0000u, E2I_SEG_IRAM},	  /* IRAM */
    {0x400D0000u, 0x40400000u, E2I_SEG_IROM},	  /* IROM */
    {0x400C0000u, 0x400C2000u, E2I_SEG_RTC_DATA}, /* RTC_IRAM */
    {0x50000000u, 0x50002000u, E2I_SEG_RTC_DATA}, /* RTC_DATA (slow) */
    SEG_END,
};

/* ESP32-S2
 * Source: esptool/targets/esp32s2.py. */
static const struct seg_range r_esp32s2[] = {
    {0x3F000000u, 0x3F3F0000u, E2I_SEG_DROM},
    {0x3FFB0000u, 0x40000000u, E2I_SEG_DRAM},
    {0x40020000u, 0x40070000u, E2I_SEG_IRAM},
    {0x40080000u, 0x40B80000u, E2I_SEG_IROM},
    {0x50000000u, 0x50002000u, E2I_SEG_RTC_DATA},
    SEG_END,
};

/* ESP32-S3
 * Source: esptool/targets/esp32s3.py. */
static const struct seg_range r_esp32s3[] = {
    {0x3C000000u, 0x3E000000u, E2I_SEG_DROM},
    {0x3FC88000u, 0x3FD00000u, E2I_SEG_DRAM},
    {0x40370000u, 0x403E0000u, E2I_SEG_IRAM},
    {0x42000000u, 0x44000000u, E2I_SEG_IROM},
    {0x600FE000u, 0x60100000u, E2I_SEG_RTC_DATA},
    SEG_END,
};

/* ESP32-C2
 * Source: esptool/targets/esp32c2.py. */
static const struct seg_range r_esp32c2[] = {
    {0x3C000000u, 0x3C400000u, E2I_SEG_DROM},
    {0x3FCA0000u, 0x3FCE0000u, E2I_SEG_DRAM},
    {0x4037C000u, 0x403C0000u, E2I_SEG_IRAM},
    {0x42000000u, 0x42400000u, E2I_SEG_IROM},
    SEG_END,
};

/* ESP32-C3
 * Source: esptool/targets/esp32c3.py. */
static const struct seg_range r_esp32c3[] = {
    {0x3C000000u, 0x3C800000u, E2I_SEG_DROM},
    {0x3FC80000u, 0x3FCE0000u, E2I_SEG_DRAM},
    {0x4037C000u, 0x403E0000u, E2I_SEG_IRAM},
    {0x42000000u, 0x42800000u, E2I_SEG_IROM},
    {0x50000000u, 0x50002000u, E2I_SEG_RTC_DATA},
    SEG_END,
};

/* ESP32-C5
 * Source: esptool/targets/esp32c5.py.  DROM and IROM share one
 * unified window; DRAM and IRAM share one unified window. */
static const struct seg_range r_esp32c5[] = {
    {0x42000000u, 0x44000000u, E2I_SEG_DROM}, /* DROM == IROM window */
    {0x40800000u, 0x40860000u, E2I_SEG_DRAM}, /* DRAM == IRAM window */
    {0x50000000u, 0x50004000u, E2I_SEG_RTC_DATA},
    SEG_END,
};

/* ESP32-C6
 * Source: esptool/targets/esp32c6.py.  IROM and DROM are adjacent
 * non-overlapping windows. */
static const struct seg_range r_esp32c6[] = {
    {0x42000000u, 0x42800000u, E2I_SEG_IROM},
    {0x42800000u, 0x43000000u, E2I_SEG_DROM},
    {0x40800000u, 0x40880000u, E2I_SEG_DRAM}, /* DRAM == IRAM window */
    {0x50000000u, 0x50004000u, E2I_SEG_RTC_DATA},
    SEG_END,
};

/* ESP32-H2
 * Source: esptool/targets/esp32h2.py (inherits ESP32-C6 ranges). */
static const struct seg_range r_esp32h2[] = {
    {0x42000000u, 0x42800000u, E2I_SEG_IROM},
    {0x42800000u, 0x43000000u, E2I_SEG_DROM},
    {0x40800000u, 0x40880000u, E2I_SEG_DRAM},
    {0x50000000u, 0x50004000u, E2I_SEG_RTC_DATA},
    SEG_END,
};

/* ESP32-P4
 * Source: esptool/targets/esp32p4.py.  DROM and IROM share a huge
 * unified window; DRAM and IRAM share one window. */
static const struct seg_range r_esp32p4[] = {
    {0x40000000u, 0x4C000000u, E2I_SEG_DROM}, /* DROM == IROM window */
    {0x4FF00000u, 0x4FFA0000u, E2I_SEG_DRAM}, /* DRAM == IRAM window */
    {0x50108000u, 0x50110000u, E2I_SEG_RTC_DATA},
    SEG_END,
};

/* ------------------------------------------------------------------ */
/* Per-chip flash param tables                                        */
/* ------------------------------------------------------------------ */

/* Flash mode encoding is the same on every chip: QIO=0, QOUT=1, DIO=2,
 * DOUT=3.  "keep" is not a real byte — it would mean "leave the byte
 * that's already in flash untouched" which makes no sense for a newly
 * built image, so we reject it here with a clear error. */
struct kv_u8 {
	const char *k;
	uint8_t v;
};

static const struct kv_u8 flash_modes[] = {
    {"qio", 0}, {"qout", 1}, {"dio", 2}, {"dout", 3}, {NULL, 0},
};

/* flash_size (high nibble of byte 3) — same table for all ESP32+
 * chips; the 128MB..32MB values apply only to ESP32 family which
 * esptool allows on ESP32 only, but we accept them globally for
 * simplicity and let the ROM bootloader sort it out. */
static const struct kv_u8 flash_sizes[] = {
    {"1MB", 0x00},  {"2MB", 0x10},   {"4MB", 0x20},
    {"8MB", 0x30},  {"16MB", 0x40},  {"32MB", 0x50},
    {"64MB", 0x60}, {"128MB", 0x70}, {NULL, 0},
};

/*
 * flash_freq (low nibble of byte 3) — chip-specific.  Esptool's
 * targets/esp*.py FLASH_FREQUENCY dicts translate straight into
 * these tables.
 */
static const struct kv_u8 freq_esp32[] = {
    {"80m", 0xF}, {"40m", 0x0}, {"26m", 0x1}, {"20m", 0x2}, {NULL, 0},
};
/* ESP32-S2/S3/C3/P4 share ESP32's freq table. */
#define freq_esp32s2 freq_esp32
#define freq_esp32s3 freq_esp32
#define freq_esp32c3 freq_esp32
#define freq_esp32p4 freq_esp32

static const struct kv_u8 freq_esp32c2[] = {
    {"60m", 0xF}, {"30m", 0x0}, {"20m", 0x1}, {"15m", 0x2}, {NULL, 0},
};

static const struct kv_u8 freq_esp32c5[] = {
    {"80m", 0xF},
    {"40m", 0x0},
    {"20m", 0x2},
    {NULL, 0},
};

/* ESP32-C6 quirk: ROM workaround maps 80m to 0x0 (same as 40m). */
static const struct kv_u8 freq_esp32c6[] = {
    {"80m", 0x0},
    {"40m", 0x0},
    {"20m", 0x2},
    {NULL, 0},
};

static const struct kv_u8 freq_esp32h2[] = {
    {"48m", 0xF}, {"24m", 0x0}, {"16m", 0x1}, {"12m", 0x2}, {NULL, 0},
};

/* ------------------------------------------------------------------ */
/* Top-level chip table                                               */
/* ------------------------------------------------------------------ */

struct chip_info {
	const char *name;
	uint16_t chip_id;
	const struct seg_range *ranges;
	const struct kv_u8 *freqs;
};

static const struct chip_info chips[E2I_CHIP_MAX] = {
    [E2I_CHIP_ESP32] = {"esp32", 0x0000, r_esp32, freq_esp32},
    [E2I_CHIP_ESP32S2] = {"esp32s2", 0x0002, r_esp32s2, freq_esp32s2},
    [E2I_CHIP_ESP32S3] = {"esp32s3", 0x0009, r_esp32s3, freq_esp32s3},
    [E2I_CHIP_ESP32C2] = {"esp32c2", 0x000C, r_esp32c2, freq_esp32c2},
    [E2I_CHIP_ESP32C3] = {"esp32c3", 0x0005, r_esp32c3, freq_esp32c3},
    [E2I_CHIP_ESP32C5] = {"esp32c5", 0x0017, r_esp32c5, freq_esp32c5},
    [E2I_CHIP_ESP32C6] = {"esp32c6", 0x000D, r_esp32c6, freq_esp32c6},
    [E2I_CHIP_ESP32H2] = {"esp32h2", 0x0010, r_esp32h2, freq_esp32h2},
    [E2I_CHIP_ESP32P4] = {"esp32p4", 0x0012, r_esp32p4, freq_esp32p4},
};

/* ------------------------------------------------------------------ */
/* Public: chip-name helpers                                          */
/* ------------------------------------------------------------------ */

enum e2i_chip e2i_chip_by_name(const char *name)
{
	if (name == NULL)
		return E2I_CHIP_MAX;
	for (int i = 0; i < E2I_CHIP_MAX; i++)
		if (!strcmp(chips[i].name, name))
			return (enum e2i_chip)i;
	return E2I_CHIP_MAX;
}

const char *e2i_chip_name(enum e2i_chip chip)
{
	if ((unsigned)chip >= (unsigned)E2I_CHIP_MAX)
		return "?";
	return chips[chip].name;
}

const char *const *e2i_chip_names(void)
{
	static const char *names[E2I_CHIP_MAX + 1];
	static int initialised;

	if (!initialised) {
		for (int i = 0; i < E2I_CHIP_MAX; i++)
			names[i] = chips[i].name;
		names[E2I_CHIP_MAX] = NULL;
		initialised = 1;
	}
	return names;
}

/* ------------------------------------------------------------------ */
/* Helpers: kv lookup, LE writes, segment classify                    */
/* ------------------------------------------------------------------ */

static uint8_t kv_lookup(const struct kv_u8 *tbl, const char *key,
			 const char *what, const char *chip_name)
{
	if (key == NULL)
		die("e2i: %s is not set (chip=%s)", what, chip_name);
	for (; tbl->k != NULL; tbl++)
		if (!strcmp(tbl->k, key))
			return tbl->v;
	die("e2i: unsupported %s '%s' for chip %s", what, key, chip_name);
	return 0; /* unreachable */
}

static void put_u8(struct sbuf *out, uint8_t v) { sbuf_addch(out, (int)v); }

static void put_le16(struct sbuf *out, uint16_t v)
{
	uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
	sbuf_add(out, b, sizeof b);
}

static void put_le32(struct sbuf *out, uint32_t v)
{
	uint8_t b[4] = {
	    (uint8_t)v,
	    (uint8_t)(v >> 8),
	    (uint8_t)(v >> 16),
	    (uint8_t)(v >> 24),
	};
	sbuf_add(out, b, sizeof b);
}

static void put_zeros(struct sbuf *out, size_t n)
{
	sbuf_grow(out, n);
	memset(out->buf + out->len, 0, n);
	sbuf_setlen(out, out->len + n);
}

static enum e2i_seg_type classify(enum e2i_chip chip, uint32_t vaddr)
{
	const struct seg_range *r = chips[chip].ranges;

	for (; r->type != E2I_SEG_UNKNOWN; r++)
		if (vaddr >= r->vaddr_lo && vaddr < r->vaddr_end)
			return r->type;
	return E2I_SEG_UNKNOWN;
}

static bool is_flash_type(enum e2i_seg_type t)
{
	return t == E2I_SEG_DROM || t == E2I_SEG_IROM;
}

/* ------------------------------------------------------------------ */
/* Segment collection                                                 */
/* ------------------------------------------------------------------ */

/*
 * Internal per-image-segment descriptor.  Backed by either the
 * original ELF PT_LOAD data or a heap-allocated zero-pad buffer.
 */
struct img_seg {
	uint32_t vaddr;	      /* load address written to the seg header */
	const uint8_t *data;  /* pointer to data (ELF buffer or pad buf) */
	uint32_t size;	      /* size of data in bytes (pre-padding) */
	uint32_t padded_size; /* size rounded up to 4 bytes */
	bool is_flash;	      /* flash-mapped (DROM/IROM) */
	/* When split_image() carves a RAM segment to fill a gap, an
	 * "owner" points at the malloc'd zero-pad buffer that holds
	 * generated padding (for freeing later).  NULL for ELF-backed. */
	uint8_t *owned;
};

static void collect_loads(const void *elf_data, size_t elf_len,
			  enum e2i_chip chip, struct img_seg **segs, size_t *n)
{
	struct elf_segments ph;
	size_t cap = 0;

	*segs = NULL;
	*n = 0;

	elf_read_segments(elf_data, elf_len, &ph);

	for (int i = 0; i < ph.nr; i++) {
		const struct elf_segment *p = &ph.s[i];

		if (p->type != 1) /* PT_LOAD */
			continue;
		if (p->filesz == 0)
			continue;

		if ((uint64_t)p->offset + p->filesz > elf_len)
			die("e2i: PT_LOAD segment extends past end of ELF");

		if (p->vaddr > UINT32_MAX)
			die("e2i: PT_LOAD vaddr > 32 bits (chip=%s)",
			    chips[chip].name);

		enum e2i_seg_type t = classify(chip, (uint32_t)p->vaddr);

		if (t == E2I_SEG_UNKNOWN)
			die("e2i: PT_LOAD at vaddr 0x%08x is not mapped "
			    "by chip %s",
			    (unsigned)p->vaddr, chips[chip].name);

		ALLOC_GROW(*segs, *n + 1, cap);
		struct img_seg *s = &(*segs)[(*n)++];

		s->vaddr = (uint32_t)p->vaddr;
		s->data = (const uint8_t *)elf_data + p->offset;
		s->size = (uint32_t)p->filesz;
		s->padded_size = (s->size + 3u) & ~3u;
		s->is_flash = is_flash_type(t);
		s->owned = NULL;

		if (*n > E2I_MAX_SEGS)
			die("e2i: too many PT_LOAD segments (%zu, max %u)", *n,
			    E2I_MAX_SEGS);
	}

	elf_segments_release(&ph);
}

/* ------------------------------------------------------------------ */
/* IROM_ALIGN placement                                               */
/* ------------------------------------------------------------------ */

/*
 * Required file position (of the segment DATA, not header) for a
 * flash segment at vaddr:
 *     data_pos % IROM_ALIGN == vaddr % IROM_ALIGN
 *
 * The segment header lives at data_pos - SEG_HDR_LEN.  Given a
 * current file position @p cur and a target vaddr, return how many
 * bytes of padding (at the header level) must precede the header so
 * data alignment works out.
 */
static uint32_t pad_needed(uint32_t cur, uint32_t vaddr)
{
	uint32_t req_data_mod = vaddr % E2I_IROM_ALIGN;
	uint32_t req_hdr_mod =
	    (req_data_mod + E2I_IROM_ALIGN - E2I_SEG_HDR_LEN) % E2I_IROM_ALIGN;
	uint32_t cur_mod = cur % E2I_IROM_ALIGN;

	if (cur_mod <= req_hdr_mod)
		return req_hdr_mod - cur_mod;
	return E2I_IROM_ALIGN - cur_mod + req_hdr_mod;
}

/*
 * Insert zero-padding segments (or split a RAM segment) so that the
 * next flash segment's data lands on its required IROM_ALIGN offset.
 *
 * Algorithm mirrors esptool bin_image.py:898-924.  Between each flash
 * segment we compute how much padding is needed; that gap is filled
 * by splitting the next RAM segment if available (split_image), or
 * by synthesising a zero-byte padding segment.
 */
static void layout(struct img_seg **segs_io, size_t *n_io, uint32_t header_size)
{
	struct img_seg *segs = *segs_io;
	size_t n = *n_io;
	size_t cap = n;
	uint32_t pos = header_size;
	size_t ram_cursor = 0;

	/* The working array may grow when we synthesise zero-pads or
	 * split RAM segments. */
	struct img_seg *out = NULL;
	size_t out_n = 0;
	size_t out_cap = 0;

	/* Advance the RAM cursor past any leading flash segments. */
	while (ram_cursor < n && segs[ram_cursor].is_flash)
		ram_cursor++;

	for (size_t i = 0; i < n; i++) {
		if (!segs[i].is_flash) {
			/* Non-flash segments are pushed when we reach
			 * them via the ram_cursor walk below.  Skip
			 * here; they flow out in order. */
			continue;
		}

		uint32_t gap = pad_needed(pos, segs[i].vaddr);

		/* Fill the gap with remaining RAM segments (splitting
		 * if necessary) or a synthesised pad. */
		while (gap > 0) {
			/* Skip past RAM segments we've already emitted. */
			while (ram_cursor < n && segs[ram_cursor].is_flash)
				ram_cursor++;

			if (ram_cursor < n && gap > E2I_SEG_HDR_LEN) {
				/* Use (part of) the next RAM segment. */
				struct img_seg *src = &segs[ram_cursor];
				uint32_t avail = src->padded_size;
				uint32_t cap_data = gap - E2I_SEG_HDR_LEN;

				if (avail <= cap_data) {
					/* Whole segment fits. */
					ALLOC_GROW(out, out_n + 1, out_cap);
					out[out_n++] = *src;
					pos += E2I_SEG_HDR_LEN + avail;
					gap -= E2I_SEG_HDR_LEN + avail;
					ram_cursor++;
				} else {
					/* Split: emit cap_data bytes as
					 * its own segment (sized down),
					 * keep the remainder. */
					struct img_seg head = *src;
					head.size = cap_data;
					head.padded_size = cap_data;
					ALLOC_GROW(out, out_n + 1, out_cap);
					out[out_n++] = head;
					/* Update src to be the tail. */
					src->data += cap_data;
					src->vaddr += cap_data;
					src->size -= cap_data;
					src->padded_size -= cap_data;
					pos += E2I_SEG_HDR_LEN + cap_data;
					gap -= E2I_SEG_HDR_LEN + cap_data;
				}
			} else {
				/* Can't fit another header, or no RAM
				 * segments left — emit a zero-pad seg. */
				uint32_t pad_len = (gap < E2I_SEG_HDR_LEN)
						       ? 0u
						       : gap - E2I_SEG_HDR_LEN;
				struct img_seg pad = {0};

				pad.vaddr = 0;
				pad.size = pad_len;
				pad.padded_size = pad_len;
				pad.is_flash = false;
				if (pad_len > 0) {
					pad.owned = malloc(pad_len);
					if (pad.owned == NULL)
						die_errno("malloc(%u)",
							  pad_len);
					memset(pad.owned, 0, pad_len);
					pad.data = pad.owned;
				} else {
					pad.data = NULL;
				}
				ALLOC_GROW(out, out_n + 1, out_cap);
				out[out_n++] = pad;
				pos += E2I_SEG_HDR_LEN + pad_len;
				gap = 0;
			}
		}

		/* Now emit the flash segment itself. */
		ALLOC_GROW(out, out_n + 1, out_cap);
		out[out_n++] = segs[i];
		pos += E2I_SEG_HDR_LEN + segs[i].padded_size;
	}

	/* Emit any trailing RAM segments. */
	while (ram_cursor < n) {
		if (segs[ram_cursor].is_flash) {
			ram_cursor++;
			continue;
		}
		ALLOC_GROW(out, out_n + 1, out_cap);
		out[out_n++] = segs[ram_cursor++];
	}

	if (out_n > E2I_MAX_SEGS)
		die("e2i: layout produced %zu image segments (max %u); "
		    "too many flash regions for this chip",
		    out_n, E2I_MAX_SEGS);

	/* Free the old outer array and swap. */
	free(segs);
	*segs_io = out;
	*n_io = out_n;
	(void)cap;
}

/* ------------------------------------------------------------------ */
/* Write pass + checksum + digest                                     */
/* ------------------------------------------------------------------ */

static void write_segs(struct sbuf *out, struct img_seg *segs, size_t n,
		       uint32_t *checksum_io, uint8_t elf_hash[E2I_DIGEST_LEN],
		       uint32_t elf_sha256_offset)
{
	uint32_t cksum = *checksum_io;

	for (size_t i = 0; i < n; i++) {
		struct img_seg *s = &segs[i];
		uint32_t start_off = (uint32_t)out->len;

		put_le32(out, s->vaddr);
		put_le32(out, s->padded_size);

		/* Copy data, then zero-pad to padded_size.  Each byte
		 * contributes to the running XOR checksum. */
		if (s->size > 0)
			sbuf_add(out, s->data, s->size);
		if (s->padded_size > s->size)
			put_zeros(out, s->padded_size - s->size);

		/* If the app_elf_sha256 field lives inside this
		 * segment's data, patch it in-place in the output
		 * buffer.  The field is 32 bytes of zeros in the ELF
		 * until we patch it. */
		if (elf_sha256_offset != 0 && elf_hash != NULL) {
			uint32_t data_start = start_off + E2I_SEG_HDR_LEN;
			uint32_t data_end = data_start + s->padded_size;

			if (elf_sha256_offset + E2I_DIGEST_LEN <= data_end &&
			    elf_sha256_offset >= data_start) {
				memcpy(out->buf + elf_sha256_offset, elf_hash,
				       E2I_DIGEST_LEN);
			}
		}

		/* Recompute XOR over whatever data we just wrote
		 * (reading back from the buffer is easiest because we
		 * may have patched the ELF SHA above). */
		for (uint32_t j = 0; j < s->padded_size; j++)
			cksum ^=
			    (uint8_t)out->buf[start_off + E2I_SEG_HDR_LEN + j];
	}

	*checksum_io = cksum;
}

static void sha256_buf(const uint8_t *data, size_t n,
		       uint8_t hash[E2I_DIGEST_LEN])
{
	SHA256_CTX ctx;

	sha256_init(&ctx);
	sha256_update(&ctx, data, n);
	sha256_final(&ctx, hash);
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

void e2i_build(const void *elf, size_t elf_len, enum e2i_chip chip,
	       const struct e2i_config *cfg, struct sbuf *out)
{
	if ((unsigned)chip >= (unsigned)E2I_CHIP_MAX)
		die("e2i: invalid chip (%d)", (int)chip);
	if (cfg == NULL)
		die("e2i: config must not be NULL");

	const struct chip_info *ci = &chips[chip];

	/* Validate flash params up front so the error message comes
	 * before any output is produced. */
	uint8_t fm_byte =
	    kv_lookup(flash_modes, cfg->flash_mode, "flash-mode", ci->name);
	uint8_t fs_byte =
	    kv_lookup(flash_sizes, cfg->flash_size, "flash-size", ci->name);
	uint8_t ff_byte =
	    kv_lookup(ci->freqs, cfg->flash_freq, "flash-freq", ci->name);

	/* Read entry point from ELF Ehdr and collect PT_LOAD segments. */
	struct elf_segments ph;
	elf_read_segments(elf, elf_len, &ph);
	uint32_t entry = (uint32_t)ph.entry;
	elf_segments_release(&ph);

	struct img_seg *segs;
	size_t n_segs;

	collect_loads(elf, elf_len, chip, &segs, &n_segs);
	if (n_segs == 0)
		die("e2i: ELF has no PT_LOAD segments with data");

	/* Compute the ELF SHA once (optional, only when requested). */
	uint8_t elf_hash[E2I_DIGEST_LEN];
	bool want_elf_patch = cfg->elf_sha256_offset != 0;
	if (want_elf_patch)
		sha256_buf(elf, elf_len, elf_hash);

	/*
	 * Reset the output sbuf.  Image construction proceeds in one
	 * linear pass — we don't need to back up or rewrite except
	 * for the segment-count byte at header offset 1 (patched at
	 * the end after layout() has settled on the final count).
	 */
	sbuf_reset(out);

	uint32_t header_size = E2I_HDR_LEN + E2I_EXT_HDR_LEN;
	layout(&segs, &n_segs, header_size);

	/* --- Common header (8 bytes) --- */
	put_u8(out, E2I_IMAGE_MAGIC);
	put_u8(out,
	       (uint8_t)n_segs); /* patched below if layout changes count */
	put_u8(out, fm_byte);
	put_u8(out, (uint8_t)(fs_byte | ff_byte));
	put_le32(out, entry);

	/* --- Extended header (16 bytes) --- */
	put_u8(out, 0xEE); /* WP pin (disabled) */
	put_u8(out, 0);	   /* SPI drv: clk/q */
	put_u8(out, 0);	   /* SPI drv: d/cs */
	put_u8(out, 0);	   /* SPI drv: hd/wp */
	put_le16(out, ci->chip_id);
	put_u8(out, 0); /* min_rev (legacy byte) */
	put_le16(out, cfg->min_rev_full);
	put_le16(out, cfg->max_rev_full);
	put_zeros(out, 4); /* reserved */
	put_u8(out, cfg->append_sha256 ? 1u : 0u);

	/* --- Segments + running checksum --- */
	uint32_t cksum = E2I_CHECKSUM_MAGIC;
	write_segs(out, segs, n_segs, &cksum, want_elf_patch ? elf_hash : NULL,
		   cfg->elf_sha256_offset);

	/* Patch segment count (header byte 1). */
	if (n_segs <= 0xFF)
		out->buf[1] = (char)(uint8_t)n_segs;

	/*
	 * Checksum trailer: pad zeros until the next byte position is
	 * 15 mod 16, then write the 1-byte XOR checksum.  Result:
	 * out->len % 16 == 0 after.
	 */
	size_t pad = (size_t)(16u - ((out->len + 1u) % 16u)) % 16u;
	put_zeros(out, pad);
	put_u8(out, (uint8_t)cksum);

	/* --- Optional SHA-256 digest over everything so far --- */
	if (cfg->append_sha256) {
		uint8_t image_hash[E2I_DIGEST_LEN];

		sha256_buf((const uint8_t *)out->buf, out->len, image_hash);
		sbuf_add(out, image_hash, sizeof image_hash);
	}

	/* Free the layout array (including any owned zero-pads). */
	for (size_t i = 0; i < n_segs; i++)
		free(segs[i].owned);
	free(segs);
}
