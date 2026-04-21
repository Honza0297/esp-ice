/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/__complete/__complete.c
 * @brief Hidden `ice __complete` -- shell-completion backend.
 *
 * The shell-side completion scripts (see `ice completion`) invoke
 * this command to ask for candidates.  It rebuilds the argv context
 * before the cursor, appends @b{--ice-complete}, and re-enters
 * @c cmd_ice() so the normal dispatcher walks to the right leaf; the
 * leaf's @c parse_options() sees @b{--ice-complete}, dumps candidates,
 * and exits.  The shell does prefix filtering afterwards.
 *
 * Hidden from @c ice --help and @c ice <TAB> because the name starts
 * with an underscore.
 */
#include "ice.h"

static const struct cmd_manual complete_manual = {.name = "ice __complete"};
static const struct option cmd___complete_opts[] = {OPT_END()};

int cmd_complete(int argc, const char **argv);

const struct cmd_desc cmd___complete_desc = {
    .name = "__complete",
    .fn = cmd_complete,
    .opts = cmd___complete_opts,
    .manual = &complete_manual,
};

/**
 * argv layout:  ["__complete", "<cword>", "ice", "target", "set", "es"]
 *
 * We take words[0..cword-1] (the resolved context before the cursor
 * word), append "--ice-complete", and call cmd_ice().  The normal
 * dispatch chain routes through subcommands until the deepest
 * parse_options() sees --ice-complete, dumps candidates, and exits.
 */
int cmd_complete(int argc, const char **argv)
{
	struct svec av = SVEC_INIT;
	int cword;
	int nwords;
	const char **words;

	/* argv[0]="__complete", argv[1]=cword, argv[2..]=words. */
	if (argc < 3)
		return EXIT_SUCCESS;

	cword = atoi(argv[1]);
	nwords = argc - 2;
	words = argv + 2;

	if (cword < 0 || cword >= nwords)
		return EXIT_SUCCESS;

	/* Build argv: words[0..cword-1] + "--ice-complete". */
	for (int i = 0; i < cword; i++)
		svec_push(&av, words[i]);
	svec_push(&av, "--ice-complete");

	cmd_ice((int)av.nr, (const char **)av.v);

	/* cmd_ice -> ... -> parse_options -> exit(0); normally unreached. */
	svec_clear(&av);
	return EXIT_SUCCESS;
}
