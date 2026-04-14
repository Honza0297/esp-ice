/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for elf.c -- the ELF section-header reader.
 *
 * Fixture `stub.o` is produced by the `.t` wrapper from a small C
 * translation unit; this test loads it and verifies that the expected
 * sections are reachable with sensible type/size values.
 */
#include "ice.h"
#include "tap.h"

#ifndef SHT_PROGBITS
#define SHT_PROGBITS 1
#endif
#ifndef SHT_NOBITS
#define SHT_NOBITS 8
#endif

static const struct elf_section *find(const struct elf_sections *ss,
				      const char *name)
{
	for (int i = 0; i < ss->nr; i++)
		if (!strcmp(ss->s[i].name, name))
			return &ss->s[i];
	return NULL;
}

int main(void)
{
	struct sbuf sb = SBUF_INIT;
	struct elf_sections ss;
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
	return tap_result();
}
