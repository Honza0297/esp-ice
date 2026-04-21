/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file report.c
 * @brief Deferred diagnostic reporting for the Kconfig processor.
 *
 * Non-fatal diagnostics are collected during parsing and evaluation,
 * then flushed (printed to stderr sorted by file:line) at the end.
 * Fatal errors still use die() for immediate termination.
 */
#include "cconfig/cconfig.h"
#include "ice.h"

#include <stdarg.h>

void kc_report_init(struct kc_report *rpt) { memset(rpt, 0, sizeof(*rpt)); }

void kc_report_release(struct kc_report *rpt)
{
	size_t idx;

	for (idx = 0; idx < rpt->count; idx++)
		free(rpt->msgs[idx].text);
	free(rpt->msgs);
	memset(rpt, 0, sizeof(*rpt));
}

static void report_add(struct kc_report *rpt, enum kc_report_level level,
		       const char *file, int line, const char *fmt, va_list ap)
{
	struct kc_report_msg *msg;
	struct sbuf sb = SBUF_INIT;

	ALLOC_GROW(rpt->msgs, rpt->count + 1, rpt->alloc);

	sbuf_vaddf(&sb, fmt, ap);

	msg = &rpt->msgs[rpt->count];
	msg->level = level;
	msg->file = file;
	msg->line = line;
	msg->text = sbuf_detach(&sb);
	msg->seq = rpt->count;
	rpt->count++;

	if (level == KC_REPORT_ERROR)
		rpt->error_count++;
}

void kc_report_error(struct kc_report *rpt, const char *file, int line,
		     const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report_add(rpt, KC_REPORT_ERROR, file, line, fmt, ap);
	va_end(ap);
}

void kc_report_warning(struct kc_report *rpt, const char *file, int line,
		       const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report_add(rpt, KC_REPORT_WARNING, file, line, fmt, ap);
	va_end(ap);
}

void kc_report_info(struct kc_report *rpt, const char *file, int line,
		    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report_add(rpt, KC_REPORT_INFO, file, line, fmt, ap);
	va_end(ap);
}

static int msg_cmp(const void *va, const void *vb)
{
	const struct kc_report_msg *msg_a = va;
	const struct kc_report_msg *msg_b = vb;
	int result;

	/* NULL file sorts before any filename */
	if (msg_a->file && msg_b->file) {
		result = strcmp(msg_a->file, msg_b->file);
		if (result)
			return result;
	} else if (!msg_a->file && msg_b->file) {
		return -1;
	} else if (msg_a->file && !msg_b->file) {
		return 1;
	}

	if (msg_a->line != msg_b->line)
		return msg_a->line - msg_b->line;

	/* Severity tiebreaker: errors before warnings before info */
	if (msg_a->level != msg_b->level)
		return (int)msg_a->level - (int)msg_b->level;

	/* Insertion-order tiebreaker for stability (qsort is not stable) */
	return (msg_a->seq < msg_b->seq) ? -1 : 1;
}

static const char *level_str(enum kc_report_level level)
{
	switch (level) {
	case KC_REPORT_ERROR:
		return "error";
	case KC_REPORT_WARNING:
		return "warning";
	case KC_REPORT_INFO:
		return "info";
	}
	return "unknown";
}

int kc_report_flush(struct kc_report *rpt)
{
	size_t idx;
	int errors = 0;

	qsort(rpt->msgs, rpt->count, sizeof(rpt->msgs[0]), msg_cmp);

	for (idx = 0; idx < rpt->count; idx++) {
		const struct kc_report_msg *msg = &rpt->msgs[idx];

		if (msg->level == KC_REPORT_ERROR)
			errors++;

		fprintf(stderr, "%s: %s:%d: %s\n", level_str(msg->level),
			msg->file ? msg->file : "<unknown>", msg->line,
			msg->text);
	}

	return errors;
}

int kc_report_has_errors(const struct kc_report *rpt)
{
	return rpt->error_count > 0;
}
