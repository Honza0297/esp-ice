/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file csv.h
 * @brief CSV reader / writer (RFC 4180 subset + '#' comments).
 *
 * Dialect:
 *  - Fields separated by ','.
 *  - Records separated by LF (CRLF also accepted on read).
 *  - A field may be enclosed in double quotes: "hello, world".
 *    Inside a quoted field, "" denotes a literal ".
 *  - An unquoted field's leading and trailing whitespace is trimmed;
 *    a quoted field's content is preserved byte-for-byte.
 *  - '#' outside a quoted field starts a comment that runs to end of
 *    line (inline too).  Inside quotes it is literal.
 *  - Quoted newlines are NOT supported -- each record must fit on one
 *    source line.
 *
 * The writer always quotes, escaping interior " as "".  Any field
 * that doesn't contain CR or LF therefore round-trips exactly through
 * csv_save() + csv_load().
 *
 * Usage -- reading:
 *   struct csv c = CSV_INIT;
 *   csv_load(&c, path);
 *   for (int i = 0; i < c.nr; i++) {
 *       const struct csv_record *r = &c.records[i];
 *       if (r->nr_fields < 5)
 *           die("line %d: expected 5 fields", r->lineno);
 *       ...
 *   }
 *   csv_release(&c);
 *
 * Usage -- writing:
 *   struct csv c = CSV_INIT;
 *   struct csv_record *r = csv_add_record(&c);
 *   csv_record_add(r, "nvs");
 *   csv_record_addf(r, "0x%x", 0x9000);
 *   csv_save(&c, path);
 *   csv_release(&c);
 */
#ifndef CSV_H
#define CSV_H

struct sbuf; /* csv_serialize() parameter; defined in sbuf.h. */

struct csv_record {
	char **fields;	  /**< Trimmed field strings, owned by the csv. */
	int nr_fields;	  /**< Number of fields. */
	int alloc_fields; /**< Internal: capacity of fields[]. */
	int lineno;	  /**< 1-based source line (0 for in-memory records). */
};

struct csv {
	struct csv_record *records;
	int nr;
	int alloc;
};

/** Static initializer. */
#define CSV_INIT {.records = NULL, .nr = 0, .alloc = 0}

/** Initialize an empty csv (equivalent to CSV_INIT). */
void csv_init(struct csv *c);

/** Free every record and reset to empty. */
void csv_release(struct csv *c);

/**
 * @brief Read @p path and append the parsed records to @p c.
 *
 * Multiple csv_load() calls accumulate.  On I/O error @p c is left
 * unmodified (no partial-parse residue).
 *
 * @return 0 on success, -1 on I/O error (errno is set).
 */
int csv_load(struct csv *c, const char *path);

/**
 * @brief Append a new empty record to @p c.
 *
 * @return Pointer to the new record (owned by @p c); populate it with
 *         csv_record_add() / csv_record_addf().
 */
struct csv_record *csv_add_record(struct csv *c);

/** Append a field to @p r (the string is copied). */
void csv_record_add(struct csv_record *r, const char *s);

/** Append a printf-formatted field to @p r. */
void csv_record_addf(struct csv_record *r, const char *fmt, ...);

/**
 * @brief Serialize @p c into @p out using the always-quoted writer.
 *
 * Every field is wrapped in "..." and interior " are escaped as "".
 * A trailing newline is emitted after every record.
 */
void csv_serialize(const struct csv *c, struct sbuf *out);

/**
 * @brief Serialize @p c and atomically write it to @p path.
 *
 * Internally goes through write_file_atomic() (sibling ".tmp" file
 * plus rename), so a crash mid-write leaves any existing @p path
 * untouched.
 *
 * @return 0 on success, -1 on I/O error (errno is set).
 */
int csv_save(const struct csv *c, const char *path);

#endif /* CSV_H */
