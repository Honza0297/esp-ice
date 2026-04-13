/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file partition_table.c
 * @brief ESP partition table CSV parser and binary writer.
 *
 * Replaces gen_esp32part.py.  Implements the same CSV dialect and
 * the same 32-byte entry binary format, including the MD5 checksum
 * entry and 0xFF padding to PT_DATA_SIZE.
 */
#include "partition_table.h"
#include "ice.h"
#include "md5.h"

/* ------------------------------------------------------------------ */
/* Type / subtype name tables                                          */
/* ------------------------------------------------------------------ */

static const struct {
	const char *name;
	uint8_t val;
} type_names[] = {{"app", PT_TYPE_APP},
		  {"data", PT_TYPE_DATA},
		  {"bootloader", PT_TYPE_BOOT},
		  {"partition_table", PT_TYPE_PTABLE},
		  {NULL, 0}};

static const struct {
	const char *name;
	uint8_t type;
	uint8_t val;
} subtype_names[] = {
    /* app */
    {"factory", PT_TYPE_APP, 0x00},
    {"test", PT_TYPE_APP, 0x20},
    /* app ota_0..ota_15 handled specially */
    /* app tee_0..tee_1 handled specially */

    /* data */
    {"ota", PT_TYPE_DATA, 0x00},
    {"phy", PT_TYPE_DATA, 0x01},
    {"nvs", PT_TYPE_DATA, 0x02},
    {"coredump", PT_TYPE_DATA, 0x03},
    {"nvs_keys", PT_TYPE_DATA, 0x04},
    {"efuse", PT_TYPE_DATA, 0x05},
    {"undefined", PT_TYPE_DATA, 0x06},
    {"esphttpd", PT_TYPE_DATA, 0x80},
    {"fat", PT_TYPE_DATA, 0x81},
    {"spiffs", PT_TYPE_DATA, 0x82},
    {"littlefs", PT_TYPE_DATA, 0x83},
    {"tee_ota", PT_TYPE_DATA, 0x90},

    /* bootloader */
    {"primary", PT_TYPE_BOOT, 0x00},
    {"ota", PT_TYPE_BOOT, 0x01},
    {"recovery", PT_TYPE_BOOT, 0x02},

    /* partition_table */
    {"primary", PT_TYPE_PTABLE, 0x00},
    {"ota", PT_TYPE_PTABLE, 0x01},

    {NULL, 0, 0}};

/* ------------------------------------------------------------------ */
/* Integer / string parsing helpers                                    */
/* ------------------------------------------------------------------ */

/* Parse a string that may be hex (0x...), decimal, or end with K/M. */
static int parse_int(const char *s, uint32_t *out)
{
	char *end;
	unsigned long v;

	if (!s || !*s)
		return -1;

	/* K/M suffix (case-insensitive) */
	{
		size_t len = strlen(s);
		int suffix = 0;

		if (len > 1) {
			char last = s[len - 1];
			if (last == 'k' || last == 'K')
				suffix = 1024;
			if (last == 'm' || last == 'M')
				suffix = 1024 * 1024;
		}

		if (suffix) {
			char tmp[32];
			if (len - 1 >= sizeof(tmp))
				return -1;
			memcpy(tmp, s, len - 1);
			tmp[len - 1] = '\0';
			if (parse_int(tmp, out) != 0)
				return -1;
			*out *= (uint32_t)suffix;
			return 0;
		}
	}

	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno || end == s || *end != '\0')
		return -1;

	*out = (uint32_t)v;
	return 0;
}

static int parse_type(const char *s, uint8_t *out)
{
	int i;

	for (i = 0; type_names[i].name; i++) {
		if (!strcmp(s, type_names[i].name)) {
			*out = type_names[i].val;
			return 0;
		}
	}

	/* Try numeric */
	{
		uint32_t v;
		if (parse_int(s, &v) == 0 && v <= 0xFF) {
			*out = (uint8_t)v;
			return 0;
		}
	}
	return -1;
}

static int parse_subtype(uint8_t type, const char *s, uint8_t *out)
{
	int i;

	/* ota_N for app */
	if (type == PT_TYPE_APP && !strncmp(s, "ota_", 4)) {
		uint32_t slot;
		if (parse_int(s + 4, &slot) == 0 && slot < 16) {
			*out = (uint8_t)(0x10 + slot);
			return 0;
		}
		return -1;
	}

	/* tee_N for app */
	if (type == PT_TYPE_APP && !strncmp(s, "tee_", 4)) {
		uint32_t slot;
		if (parse_int(s + 4, &slot) == 0 && slot < 2) {
			*out = (uint8_t)(0x30 + slot);
			return 0;
		}
		return -1;
	}

	/* named subtypes */
	for (i = 0; subtype_names[i].name; i++) {
		if (subtype_names[i].type == type &&
		    !strcmp(s, subtype_names[i].name)) {
			*out = subtype_names[i].val;
			return 0;
		}
	}

	/* numeric */
	{
		uint32_t v;
		if (parse_int(s, &v) == 0 && v <= 0xFF) {
			*out = (uint8_t)v;
			return 0;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* CSV line parser                                                     */
/* ------------------------------------------------------------------ */

static void trim(char *s)
{
	char *p = s + strlen(s);

	while (p > s && (*(p - 1) == ' ' || *(p - 1) == '\t' ||
			 *(p - 1) == '\r' || *(p - 1) == '\n'))
		*--p = '\0';

	p = s;
	while (*p == ' ' || *p == '\t')
		p++;

	if (p != s)
		memmove(s, p, strlen(p) + 1);
}

/* Split @line on commas into at most 6 fields in-place. */
static int split_csv(char *line, char *fields[6])
{
	int n = 0;
	char *p = line;

	while (n < 6) {
		fields[n++] = p;
		p = strchr(p, ',');
		if (!p)
			break;
		*p++ = '\0';
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* Alignment helpers                                                   */
/* ------------------------------------------------------------------ */

static uint32_t offset_align_for(uint8_t type)
{
	return (type == PT_TYPE_APP) ? 0x10000u : 0x1000u;
}

static uint32_t size_align_for(uint8_t type, int secure)
{
	if (type != PT_TYPE_APP)
		return 1;
	/* secure v1: app must be 64K aligned in size */
	return (secure == PT_SECURE_V1) ? 0x10000u : 0x1000u;
}

static uint32_t align_up(uint32_t val, uint32_t align)
{
	if (!align || val % align == 0)
		return val;
	return val + (align - val % align);
}

/* ------------------------------------------------------------------ */
/* CSV parser                                                          */
/* ------------------------------------------------------------------ */

int pt_parse_csv(const char *path, struct pt_entry *entries, int *count,
		 const struct pt_options *opts)
{
	FILE *fp;
	char line[256];
	int lineno = 0;
	int n = 0;
	uint32_t last_end;

	fp = fopen(path, "r");
	if (!fp) {
		err_errno("cannot open '%s'", path);
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *fields[6];
		int nf;
		struct pt_entry *e;
		char *name_f, *type_f, *sub_f, *off_f, *size_f, *flags_f;

		lineno++;

		/* strip comment and blank */
		{
			char *c = strchr(line, '#');
			if (c)
				*c = '\0';
		}
		trim(line);
		if (!line[0])
			continue;

		if (n >= PT_MAX_ENTRIES) {
			err("line %d: too many entries (max %d)", lineno,
			    PT_MAX_ENTRIES);
			fclose(fp);
			return -1;
		}

		/* duplicate the line for splitting */
		{
			char tmp[256];
			strncpy(tmp, line, sizeof(tmp) - 1);
			tmp[sizeof(tmp) - 1] = '\0';
			nf = split_csv(tmp, fields);

			/* need at least name, type, subtype, size */
			if (nf < 5) {
				err("line %d: expected at least 5 fields: %s",
				    lineno, line);
				fclose(fp);
				return -1;
			}

			name_f = fields[0];
			type_f = fields[1];
			sub_f = fields[2];
			off_f = fields[3];
			size_f = fields[4];
			flags_f = (nf >= 6) ? fields[5] : (char *)"";

			for (int i = 0; i < nf; i++)
				trim(fields[i]);

			e = &entries[n];
			memset(e, 0, sizeof(*e));

			/* name */
			if (strlen(name_f) > 16) {
				err("line %d: name '%s' too long (max 16)",
				    lineno, name_f);
				fclose(fp);
				return -1;
			}
			strncpy(e->name, name_f, 16);

			/* type */
			if (parse_type(type_f, &e->type) != 0) {
				err("line %d: unknown type '%s'", lineno,
				    type_f);
				fclose(fp);
				return -1;
			}

			/* subtype */
			if (sub_f[0]) {
				if (parse_subtype(e->type, sub_f,
						  &e->subtype) != 0) {
					err("line %d: unknown subtype '%s'",
					    lineno, sub_f);
					fclose(fp);
					return -1;
				}
			} else {
				/* empty subtype: app must be explicit */
				if (e->type == PT_TYPE_APP) {
					err("line %d: app partition must have "
					    "a subtype",
					    lineno);
					fclose(fp);
					return -1;
				}
				e->subtype = 0x06; /* 'undefined' */
			}

			/* offset */
			if (off_f[0]) {
				/* bootloader/ptable offsets are overridden by
				 * CLI */
				if ((e->type == PT_TYPE_BOOT &&
				     (e->subtype == 0x00 /* primary */ ||
				      e->subtype == 0x02 /* recovery */)) ||
				    (e->type == PT_TYPE_PTABLE &&
				     e->subtype == 0x00 /* primary */)) {
					/* offset set by --*-offset option */
				} else {
					uint32_t v;
					if (parse_int(off_f, &v) != 0) {
						err("line %d: bad offset '%s'",
						    lineno, off_f);
						fclose(fp);
						return -1;
					}
					e->offset = v;
					e->offset_set = 1;
				}
			}

			/* size */
			if (e->type == PT_TYPE_BOOT) {
				/* size = table_offset - primary_boot_offset */
				if (!opts->has_primary_boot) {
					err("line %d: bootloader entry "
					    "requires "
					    "--primary-bootloader-offset",
					    lineno);
					fclose(fp);
					return -1;
				}
				e->size = opts->table_offset -
					  opts->primary_boot_offset;
			} else if (e->type == PT_TYPE_PTABLE) {
				e->size = PT_TABLE_SIZE;
			} else if (size_f[0]) {
				uint32_t v;
				/* negative size: fill to this address */
				if (size_f[0] == '-') {
					uint32_t end_addr;
					if (parse_int(size_f + 1, &end_addr) !=
					    0) {
						err("line %d: bad size '%s'",
						    lineno, size_f);
						fclose(fp);
						return -1;
					}
					/* size resolved after offset auto-fill
					 */
					e->size =
					    (uint32_t)(-(int32_t)end_addr);
					/* marker: MSB set → negative size */
				} else {
					if (parse_int(size_f, &v) != 0) {
						err("line %d: bad size '%s'",
						    lineno, size_f);
						fclose(fp);
						return -1;
					}
					e->size = v;
				}
			} else {
				err("line %d: size field cannot be empty",
				    lineno);
				fclose(fp);
				return -1;
			}

			/* flags */
			if (flags_f[0]) {
				char fbuf[64];
				char *tok;
				strncpy(fbuf, flags_f, sizeof(fbuf) - 1);
				fbuf[sizeof(fbuf) - 1] = '\0';
				tok = strtok(fbuf, ":");
				while (tok) {
					trim(tok);
					if (!strcmp(tok, "encrypted"))
						e->encrypted = 1;
					else if (!strcmp(tok, "readonly"))
						e->readonly = 1;
					else if (tok[0])
						warn("line %d: unknown flag "
						     "'%s'",
						     lineno, tok);
					tok = strtok(NULL, ":");
				}
			}

			n++;
		}
	}
	fclose(fp);

	/* ---- Auto-fill missing offsets --------------------------------- */

	last_end = opts->table_offset + PT_TABLE_SIZE;

	for (int i = 0; i < n; i++) {
		struct pt_entry *e = &entries[i];
		int is_prim_boot =
		    (e->type == PT_TYPE_BOOT && e->subtype == 0x00);
		int is_prim_ptbl =
		    (e->type == PT_TYPE_PTABLE && e->subtype == 0x00);

		/* Set fixed offsets for bootloader/partition_table types */
		if (is_prim_boot) {
			if (!opts->has_primary_boot) {
				err("primary bootloader entry present but "
				    "--primary-bootloader-offset not set");
				return -1;
			}
			e->offset = opts->primary_boot_offset;
			e->offset_set = 1;
			continue; /* skip from last_end tracking */
		}
		if (e->type == PT_TYPE_BOOT && e->subtype == 0x02) {
			if (!opts->has_recovery_boot) {
				err("recovery bootloader entry present but "
				    "--recovery-bootloader-offset not set");
				return -1;
			}
			e->offset = opts->recovery_boot_offset;
			e->offset_set = 1;
			continue;
		}
		if (is_prim_ptbl) {
			e->offset = opts->table_offset;
			e->offset_set = 1;
			continue;
		}

		/* Normal partition: auto-fill offset if not given */
		if (!e->offset_set) {
			uint32_t align = offset_align_for(e->type);
			last_end = align_up(last_end, align);
			e->offset = last_end;
			e->offset_set = 1;
		} else if (e->offset < last_end) {
			err("partition '%s' offset 0x%x overlaps previous "
			    "partition end 0x%x",
			    e->name, e->offset, last_end);
			return -1;
		}

		/* Resolve negative size: stored as -(end_addr) in uint32 */
		if ((int32_t)e->size < 0) {
			uint32_t end_addr = (uint32_t)(-(int32_t)e->size);
			e->size = end_addr - e->offset;
		}

		last_end = e->offset + e->size;
	}

	/* ---- Validation ------------------------------------------------ */

	for (int i = 0; i < n; i++) {
		struct pt_entry *e = &entries[i];

		if (!e->size) {
			err("partition '%s' has zero size", e->name);
			return -1;
		}

		/* Offset alignment */
		{
			uint32_t align = offset_align_for(e->type);
			if (e->offset % align) {
				err("partition '%s' offset 0x%x not aligned "
				    "to 0x%x",
				    e->name, e->offset, align);
				return -1;
			}
		}

		/* Size alignment for app */
		if (e->type == PT_TYPE_APP) {
			uint32_t align = size_align_for(e->type, opts->secure);
			if (e->size % align) {
				err("partition '%s' size 0x%x not aligned "
				    "to 0x%x",
				    e->name, e->size, align);
				return -1;
			}
		}

		/* Flash size check */
		if (opts->flash_size &&
		    e->offset + e->size > opts->flash_size) {
			err("partition '%s' (0x%x + 0x%x) exceeds flash "
			    "size 0x%x",
			    e->name, e->offset, e->size, opts->flash_size);
			return -1;
		}
	}

	*count = n;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Binary writer                                                       */
/* ------------------------------------------------------------------ */

static void write_le32(uint8_t *buf, uint32_t val)
{
	buf[0] = (uint8_t)(val);
	buf[1] = (uint8_t)(val >> 8);
	buf[2] = (uint8_t)(val >> 16);
	buf[3] = (uint8_t)(val >> 24);
}

int pt_to_binary(const struct pt_entry *entries, int count,
		 const struct pt_options *opts, uint8_t out[PT_DATA_SIZE])
{
	struct md5_ctx md5;
	uint8_t *p = out;
	int i;

	memset(out, 0xFF, PT_DATA_SIZE);
	md5_init(&md5);

	for (i = 0; i < count; i++) {
		const struct pt_entry *e = &entries[i];
		uint32_t flags = 0;
		uint8_t entry[PT_ENTRY_SIZE];

		memset(entry, 0, PT_ENTRY_SIZE);

		entry[0] = 0xAA;
		entry[1] = 0x50;
		entry[2] = e->type;
		entry[3] = e->subtype;
		write_le32(entry + 4, e->offset);
		write_le32(entry + 8, e->size);
		memcpy(entry + 12, e->name, 16); /* null-padded by memset */

		if (e->encrypted)
			flags |= (1u << 0);
		if (e->readonly)
			flags |= (1u << 1);
		write_le32(entry + 28, flags);

		if (p + PT_ENTRY_SIZE > out + PT_DATA_SIZE) {
			err("partition table too large (> %d entries)",
			    PT_MAX_ENTRIES);
			return -1;
		}

		memcpy(p, entry, PT_ENTRY_SIZE);
		md5_update(&md5, entry, PT_ENTRY_SIZE);
		p += PT_ENTRY_SIZE;
	}

	/* MD5 entry */
	if (opts->md5sum) {
		uint8_t md5_entry[PT_ENTRY_SIZE];
		uint8_t digest[16];

		if (p + PT_ENTRY_SIZE > out + PT_DATA_SIZE) {
			err("partition table too large for MD5 entry");
			return -1;
		}

		md5_final(&md5, digest);

		/* Magic: 0xEB 0xEB then 14 × 0xFF */
		memset(md5_entry, 0xFF, PT_ENTRY_SIZE);
		md5_entry[0] = 0xEB;
		md5_entry[1] = 0xEB;
		memcpy(md5_entry + 16, digest, 16);

		memcpy(p, md5_entry, PT_ENTRY_SIZE);
		/* p += PT_ENTRY_SIZE; — rest already 0xFF from memset */
	}

	return 0;
}
