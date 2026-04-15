/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file csv.c
 * @brief CSV reader / writer -- see csv.h for the dialect.
 */
#include "csv.h"
#include "ice.h"

void csv_init(struct csv *c)
{
	c->records = NULL;
	c->nr = 0;
	c->alloc = 0;
}

void csv_release(struct csv *c)
{
	for (int i = 0; i < c->nr; i++) {
		struct csv_record *r = &c->records[i];
		for (int j = 0; j < r->nr_fields; j++)
			free(r->fields[j]);
		free(r->fields);
	}
	free(c->records);
	csv_init(c);
}

/* ------------------------------------------------------------------ */
/*  Reader                                                            */
/* ------------------------------------------------------------------ */

static void emit_field(struct csv_record *rec, struct sbuf *buf)
{
	ALLOC_GROW(rec->fields, rec->nr_fields + 1, rec->alloc_fields);
	rec->fields[rec->nr_fields++] = sbuf_strndup(buf->buf, buf->len);
	sbuf_reset(buf);
}

/* Trim trailing spaces/tabs in place; used for unquoted fields. */
static void rstrip_buf(struct sbuf *buf)
{
	while (buf->len > 0 && (buf->buf[buf->len - 1] == ' ' ||
				buf->buf[buf->len - 1] == '\t')) {
		buf->len--;
		buf->buf[buf->len] = '\0';
	}
}

/*
 * Parse one line into @p rec.  A small state machine so that commas,
 * '#' comments, and end-of-line are interpreted correctly inside vs
 * outside a double-quoted field.
 *
 * Leaves @p rec->nr_fields == 0 for a line that is blank or only a
 * comment; the caller is expected to discard such records.
 */
static void parse_line(struct csv_record *rec, const char *line)
{
	struct sbuf buf = SBUF_INIT;
	enum {
		EXPECT_FIELD,
		UNQUOTED,
		QUOTED,
		AFTER_QUOTE,
	} state = EXPECT_FIELD;
	int any_field = 0;

	for (; *line; line++) {
		char c = *line;

		/* '#' outside a quoted field ends the record. */
		if (state != QUOTED && c == '#')
			break;

		switch (state) {
		case EXPECT_FIELD:
			if (c == ' ' || c == '\t')
				break;
			if (c == ',') {
				emit_field(rec, &buf);
				any_field = 1;
				break;
			}
			if (c == '"') {
				state = QUOTED;
				break;
			}
			sbuf_addch(&buf, c);
			state = UNQUOTED;
			break;

		case UNQUOTED:
			if (c == ',') {
				rstrip_buf(&buf);
				emit_field(rec, &buf);
				any_field = 1;
				state = EXPECT_FIELD;
				break;
			}
			sbuf_addch(&buf, c);
			break;

		case QUOTED:
			if (c == '"') {
				if (line[1] == '"') {
					sbuf_addch(&buf, '"');
					line++;
				} else {
					state = AFTER_QUOTE;
				}
				break;
			}
			sbuf_addch(&buf, c);
			break;

		case AFTER_QUOTE:
			if (c == ',') {
				emit_field(rec, &buf);
				any_field = 1;
				state = EXPECT_FIELD;
				break;
			}
			/*
			 * Whitespace or stray text between the closing "
			 * and the next ',' is ignored -- lenient handling of
			 * `"foo"  ,` and the occasional malformed row.
			 */
			break;
		}
	}

	/* Finalize any field we were midway through. */
	switch (state) {
	case EXPECT_FIELD:
		/* Trailing comma after at least one field → empty trailing. */
		if (any_field)
			emit_field(rec, &buf);
		break;
	case UNQUOTED:
		rstrip_buf(&buf);
		emit_field(rec, &buf);
		break;
	case QUOTED:
		/* Unterminated "...: emit what we have (lenient). */
	case AFTER_QUOTE:
		emit_field(rec, &buf);
		break;
	}

	sbuf_release(&buf);
}

int csv_load(struct csv *c, const char *path)
{
	struct sbuf sb = SBUF_INIT;
	size_t pos = 0;
	char *line;
	int lineno = 0;

	if (sbuf_read_file(&sb, path) < 0) {
		sbuf_release(&sb);
		return -1;
	}

	while ((line = sbuf_getline(sb.buf, sb.len, &pos)) != NULL) {
		struct csv_record tmp = {
		    .fields = NULL,
		    .nr_fields = 0,
		    .alloc_fields = 0,
		    .lineno = ++lineno,
		};

		parse_line(&tmp, line);

		if (tmp.nr_fields > 0) {
			ALLOC_GROW(c->records, c->nr + 1, c->alloc);
			c->records[c->nr++] = tmp;
		} else {
			free(tmp.fields);
		}
	}

	sbuf_release(&sb);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Writer                                                            */
/* ------------------------------------------------------------------ */

struct csv_record *csv_add_record(struct csv *c)
{
	struct csv_record *r;

	ALLOC_GROW(c->records, c->nr + 1, c->alloc);
	r = &c->records[c->nr++];
	r->fields = NULL;
	r->nr_fields = 0;
	r->alloc_fields = 0;
	r->lineno = 0;
	return r;
}

void csv_record_add(struct csv_record *r, const char *s)
{
	ALLOC_GROW(r->fields, r->nr_fields + 1, r->alloc_fields);
	r->fields[r->nr_fields++] = sbuf_strdup(s);
}

void csv_record_addf(struct csv_record *r, const char *fmt, ...)
{
	struct sbuf sb = SBUF_INIT;
	va_list ap;

	va_start(ap, fmt);
	sbuf_vaddf(&sb, fmt, ap);
	va_end(ap);

	ALLOC_GROW(r->fields, r->nr_fields + 1, r->alloc_fields);
	r->fields[r->nr_fields++] = sbuf_strndup(sb.buf, sb.len);
	sbuf_release(&sb);
}

void csv_serialize(const struct csv *c, struct sbuf *out)
{
	for (int i = 0; i < c->nr; i++) {
		const struct csv_record *r = &c->records[i];

		for (int j = 0; j < r->nr_fields; j++) {
			const char *p = r->fields[j];

			if (j > 0)
				sbuf_addch(out, ',');
			sbuf_addch(out, '"');
			for (; *p; p++) {
				if (*p == '"')
					sbuf_addch(out, '"');
				sbuf_addch(out, *p);
			}
			sbuf_addch(out, '"');
		}
		sbuf_addch(out, '\n');
	}
}

int csv_save(const struct csv *c, const char *path)
{
	struct sbuf out = SBUF_INIT;
	FILE *fp;
	int rc = 0;

	csv_serialize(c, &out);

	fp = fopen(path, "w");
	if (!fp) {
		rc = -1;
		goto done;
	}
	if (fwrite(out.buf, 1, out.len, fp) != out.len)
		rc = -1;
	if (fclose(fp) != 0)
		rc = -1;

done:
	sbuf_release(&out);
	return rc;
}
