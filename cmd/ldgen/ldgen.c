/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ldgen.c
 * @brief Linker script generation from fragment files.
 */
#include "../../ice.h"
#include "lf.h"

int cmd_ldgen(int argc, const char **argv)
{
	int dump = 0;

	struct option opts[] = {
		OPT_BOOL('d', "dump", &dump, "dump parsed AST"),
		OPT_END(),
	};
	const char *usage[] = {
		"ice ldgen [--dump] <file.lf> [...]",
		NULL,
	};

	argc = parse_options(argc, argv, opts, usage);
	if (argc < 1)
		die("no input files; see 'ice ldgen --help'");

	for (int i = 0; i < argc; i++) {
		struct sbuf sb = SBUF_INIT;

		if (sbuf_read_file(&sb, argv[i]) < 0)
			die_errno("cannot read '%s'", argv[i]);

		struct lf_file *f = lf_parse(sb.buf, argv[i]);

		if (dump)
			lf_file_dump(f);

		int ns = 0, nc = 0, nm = 0;
		for (int j = 0; j < f->n_frags; j++) {
			switch (f->frags[j].kind) {
			case LF_SECTIONS:  ns++; break;
			case LF_SCHEME:    nc++; break;
			case LF_MAPPING:   nm++; break;
			case LF_FRAG_COND: break;
			}
		}
		printf("%s: %d sections, %d schemes, %d mappings\n",
		       argv[i], ns, nc, nm);

		lf_file_free(f);
		sbuf_release(&sb);
	}

	return 0;
}
