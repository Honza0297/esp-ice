/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_report.c
 * @brief Unit tests for the deferred diagnostic reporting subsystem.
 */
#include "cconfig/cconfig.h"
#include "ice.h"
#include "tap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: redirect stderr to a temp file, flush, read it back. */
static char *capture_flush(struct kc_report *rpt, int *ret)
{
	FILE *tmp = tmpfile();
	FILE *saved = stderr;
	long len;
	char *buf;

	stderr = tmp;
	*ret = kc_report_flush(rpt);
	fflush(stderr);
	stderr = saved;

	len = ftell(tmp);
	buf = calloc(1, (size_t)len + 1);
	rewind(tmp);
	if (len > 0)
		fread(buf, 1, (size_t)len, tmp);
	fclose(tmp);
	return buf;
}

int main(void)
{
	/* 1. Init and release empty report — no crash. */
	{
		struct kc_report rpt;
		kc_report_init(&rpt);
		tap_check(rpt.count == 0);
		tap_check(rpt.msgs == NULL);
		kc_report_release(&rpt);
		tap_done("init and release empty report");
	}

	/* 2. Add one warning, verify count, level, file, line, text. */
	{
		struct kc_report rpt;
		kc_report_init(&rpt);

		kc_report_warning(&rpt, "Kconfig.test", 42,
				  "bad default for %s", "FOO");

		tap_check(rpt.count == 1);
		tap_check(rpt.msgs[0].level == KC_REPORT_WARNING);
		tap_check(strcmp(rpt.msgs[0].file, "Kconfig.test") == 0);
		tap_check(rpt.msgs[0].line == 42);
		tap_check(strcmp(rpt.msgs[0].text, "bad default for FOO") == 0);

		kc_report_release(&rpt);
		tap_done("single warning: count, level, file, line, text");
	}

	/* 3. Multiple messages: error + warning + info, order preserved until
	 * flush. */
	{
		struct kc_report rpt;
		kc_report_init(&rpt);

		kc_report_error(&rpt, "a.kconfig", 10, "err msg");
		kc_report_warning(&rpt, "a.kconfig", 20, "warn msg");
		kc_report_info(&rpt, "a.kconfig", 30, "info msg");

		tap_check(rpt.count == 3);
		tap_check(rpt.msgs[0].level == KC_REPORT_ERROR);
		tap_check(rpt.msgs[1].level == KC_REPORT_WARNING);
		tap_check(rpt.msgs[2].level == KC_REPORT_INFO);
		tap_check(rpt.msgs[0].line == 10);
		tap_check(rpt.msgs[1].line == 20);
		tap_check(rpt.msgs[2].line == 30);

		kc_report_release(&rpt);
		tap_done("multiple messages preserve insertion order");
	}

	/* 4. kc_report_has_errors: 0 with only warnings, 1 with errors. */
	{
		struct kc_report rpt;
		kc_report_init(&rpt);

		kc_report_warning(&rpt, "x.kconfig", 1, "w1");
		kc_report_info(&rpt, "x.kconfig", 2, "i1");
		tap_check(kc_report_has_errors(&rpt) == 0);

		kc_report_error(&rpt, "x.kconfig", 3, "e1");
		tap_check(kc_report_has_errors(&rpt) == 1);

		kc_report_release(&rpt);
		tap_done("has_errors returns 0 without errors, 1 with");
	}

	/* 5. kc_report_flush returns error count. */
	{
		struct kc_report rpt;
		int errors;
		char *output;
		kc_report_init(&rpt);

		kc_report_warning(&rpt, "f.kconfig", 1, "w");
		kc_report_error(&rpt, "f.kconfig", 2, "e1");
		kc_report_error(&rpt, "f.kconfig", 3, "e2");
		kc_report_info(&rpt, "f.kconfig", 4, "i");

		output = capture_flush(&rpt, &errors);
		tap_check(errors == 2);
		free(output);

		kc_report_release(&rpt);
		tap_done("flush returns error count");
	}

	/* 6. Flush output format matches "CRITICALITY: file:line: text". */
	{
		struct kc_report rpt;
		int errors;
		char *output;
		kc_report_init(&rpt);

		kc_report_error(&rpt, "main.kconfig", 5, "symbol redefined");
		kc_report_warning(&rpt, "main.kconfig", 10,
				  "deprecated option");
		kc_report_info(&rpt, "main.kconfig", 15, "auto-selected");

		output = capture_flush(&rpt, &errors);

		tap_check(strstr(output,
				 "error: main.kconfig:5: symbol redefined\n") !=
			  NULL);
		tap_check(
		    strstr(output,
			   "warning: main.kconfig:10: deprecated option\n") !=
		    NULL);
		tap_check(
		    strstr(output, "info: main.kconfig:15: auto-selected\n") !=
		    NULL);

		free(output);
		kc_report_release(&rpt);
		tap_done(
		    "flush output format matches CRITICALITY: file:line: text");
	}

	/* 7. Release frees all message strings (no leak under ASan). */
	{
		struct kc_report rpt;
		kc_report_init(&rpt);

		kc_report_error(&rpt, "a.k", 1, "msg %d", 1);
		kc_report_warning(&rpt, "a.k", 2, "msg %d", 2);
		kc_report_info(&rpt, "a.k", 3, "msg %d", 3);

		tap_check(rpt.count == 3);
		kc_report_release(&rpt);
		tap_check(rpt.count == 0);
		tap_check(rpt.msgs == NULL);
		tap_done("release frees all message strings");
	}

	/* Sort order: messages appended in reverse file:line, sorted on flush.
	 */
	{
		struct kc_report rpt;

		kc_report_init(&rpt);
		kc_report_warning(&rpt, "z.k", 99, "last");
		kc_report_error(&rpt, "a.k", 1, "first");
		kc_report_info(&rpt, "a.k", 50, "middle");

		/* Trigger sort via flush (output goes to stderr). */
		kc_report_flush(&rpt);

		/* After sort, verify internal order. */
		tap_check(strcmp(rpt.msgs[0].file, "a.k") == 0);
		tap_check(rpt.msgs[0].line == 1);
		tap_check(strcmp(rpt.msgs[1].file, "a.k") == 0);
		tap_check(rpt.msgs[1].line == 50);
		tap_check(strcmp(rpt.msgs[2].file, "z.k") == 0);
		tap_check(rpt.msgs[2].line == 99);

		kc_report_release(&rpt);
		tap_done("flush sorts messages by file:line");
	}

	return tap_result();
}
