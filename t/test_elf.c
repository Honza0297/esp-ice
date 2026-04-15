/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for elf.c -- the ELF section- and segment-header readers.
 *
 * Fixtures are produced by the `.t` wrapper from a small C translation
 * unit: `stub.o` (relocatable .o, has sections) and `stub.elf` (linked
 * host executable, has program headers).  This test loads both and
 * verifies that the expected sections and segments are reachable.
 */
#include "ice.h"
#include "tap.h"

#ifndef SHT_PROGBITS
#define SHT_PROGBITS 1
#endif
#ifndef SHT_NOBITS
#define SHT_NOBITS 8
#endif

#ifndef PT_LOAD
#define PT_LOAD 1
#endif
#ifndef PF_X
#define PF_X 1
#define PF_W 2
#define PF_R 4
#endif

static const struct elf_section *find(const struct elf_sections *ss,
				      const char *name)
{
	for (int i = 0; i < ss->nr; i++)
		if (!strcmp(ss->s[i].name, name))
			return &ss->s[i];
	return NULL;
}

static int count_loads(const struct elf_segments *segs)
{
	int n = 0;

	for (int i = 0; i < segs->nr; i++)
		if (segs->s[i].type == PT_LOAD && segs->s[i].filesz > 0)
			n++;
	return n;
}

static int any_flag(const struct elf_segments *segs, uint32_t flag)
{
	for (int i = 0; i < segs->nr; i++)
		if (segs->s[i].type == PT_LOAD && (segs->s[i].flags & flag))
			return 1;
	return 0;
}

int main(void)
{
	struct sbuf sb = SBUF_INIT;
	struct sbuf eb = SBUF_INIT;
	struct elf_sections ss;
	struct elf_segments segs;
	const struct elf_section *text, *data, *bss;

	tap_check(sbuf_read_file(&sb, "stub.o") > 0);
	elf_read_sections(sb.buf, sb.len, &ss);
	tap_check(ss.nr > 0);
	tap_done("elf_read_sections accepts a freshly compiled .o");

	text = find(&ss, ".text");
	tap_check(text != NULL);
	tap_check(text->type == SHT_PROGBITS);
	tap_check(text->size > 0);
	tap_done(".text section is SHT_PROGBITS with a non-zero size");

	/* The stub declares an initialised int: lives in .data. */
	data = find(&ss, ".data");
	tap_check(data != NULL);
	tap_check(data->type == SHT_PROGBITS);
	tap_done(".data section is SHT_PROGBITS");

	/* The stub declares a large uninitialised array: lives in .bss. */
	bss = find(&ss, ".bss");
	tap_check(bss != NULL);
	tap_check(bss->type == SHT_NOBITS);
	tap_check(bss->size >= 1000);
	tap_done(".bss is SHT_NOBITS and covers the stub's big_array[1000]");

	elf_sections_release(&ss);
	sbuf_release(&sb);

	/* Segment reader: exercise against the linked executable. */
	tap_check(sbuf_read_file(&eb, "stub.elf") > 0);
	elf_read_segments(eb.buf, eb.len, &segs);
	tap_check(segs.nr > 0);
	tap_check(segs.entry != 0);
	tap_done("elf_read_segments populates program headers and entry");

	tap_check(count_loads(&segs) >= 1);
	tap_done("linked executable has at least one PT_LOAD with file data");

	tap_check(any_flag(&segs, PF_X));
	tap_check(any_flag(&segs, PF_R));
	tap_done("PT_LOAD segments carry PF_R and at least one is PF_X");

	elf_segments_release(&segs);
	tap_check(segs.s == NULL && segs.nr == 0 && segs.entry == 0);
	tap_done("elf_segments_release resets the struct");

	sbuf_release(&eb);
	return tap_result();
}
