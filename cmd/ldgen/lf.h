/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file lf.h
 * @brief Linker fragment (.lf) parser -- types and API.
 */
#ifndef LF_H
#define LF_H

/* ------------------------------------------------------------------ */
/*  AST                                                               */
/* ------------------------------------------------------------------ */

/**
 * An entry in any entries block.
 *
 * Which fields are populated depends on the containing fragment:
 *
 *   [sections:]  name only             (".text+", "COMMON")
 *   [scheme:]    name + target         ("text" -> "flash_text")
 *   [mapping:]   name + target + scheme ("obj":"sym" ("noflash"))
 *                name = "*" for wildcard; target = symbol or NULL
 *   archive:     name only             ("libfoo.a", "*")
 */
struct lf_entry {
	char *name;
	char *target;
	char *scheme;
};

/** One arm of an if / elif / else conditional. */
struct lf_branch {
	char *expr;		/**< condition text (NULL for else) */
	struct lf_stmt *stmts;
	int n_stmts;
};

/** A statement: either a plain entry or a conditional block. */
struct lf_stmt {
	int is_cond;
	union {
		struct lf_entry entry;
		struct {
			struct lf_branch *branches;
			int n_branches;
		} cond;
	} u;
};

/* ---- Fragments -------------------------------------------------- */

enum lf_frag_kind {
	LF_SECTIONS,
	LF_SCHEME,
	LF_MAPPING,
	LF_FRAG_COND,
};

/** A branch in a fragment-level conditional. */
struct lf_frag_branch {
	char *expr;
	struct lf_frag *frags;
	int n_frags;
};

/** A single fragment (sections / scheme / mapping / conditional). */
struct lf_frag {
	enum lf_frag_kind kind;
	union {
		struct {
			char *name;
			struct lf_stmt *stmts;
			int n;
		} sec;
		struct {
			char *name;
			struct lf_stmt *stmts;
			int n;
		} sch;
		struct {
			char *name;
			struct lf_stmt *archive;
			int n_archive;
			struct lf_stmt *entries;
			int n_entries;
		} map;
		struct {
			struct lf_frag_branch *branches;
			int n;
		} cond;
	} u;
};

/** Parsed fragment file. */
struct lf_file {
	char *path;
	struct lf_frag *frags;
	int n_frags;
};

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

/**
 * Parse a fragment file from a NUL-terminated buffer.
 * Calls die() on syntax errors.
 *
 * @param src   file contents (not modified; caller keeps ownership)
 * @param path  file path (used in error messages)
 */
struct lf_file *lf_parse(const char *src, const char *path);

/** Free all memory owned by an lf_file (including itself). */
void lf_file_free(struct lf_file *f);

/** Dump a parsed file to stdout (debugging). */
void lf_file_dump(const struct lf_file *f);

#endif /* LF_H */
