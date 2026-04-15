/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for csv.c -- reader, writer, round-trip.
 */
#include "ice.h"
#include "tap.h"

/*
 * Write CSV text to a uniquely-named temp file; return the path.
 * Each @p n selects a distinct static slot so two write_csv() calls
 * in the same test block return independently-usable strings.
 */
static const char *write_csv(int n, const char *content)
{
	static char paths[32][64];
	int slot = n % 32;
	FILE *fp;

	snprintf(paths[slot], sizeof(paths[slot]), "test_csv_%d.csv", n);
	fp = fopen(paths[slot], "w");
	if (!fp)
		return NULL;
	fputs(content, fp);
	fclose(fp);
	return paths[slot];
}

int main(void)
{
	/* Basic: three records, each with three fields. */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(1, "nvs,data,nvs\n"
						"phy_init,data,phy\n"
						"factory,app,factory\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 3);
		tap_check(c.records[0].nr_fields == 3);
		tap_check(strcmp(c.records[0].fields[0], "nvs") == 0);
		tap_check(strcmp(c.records[1].fields[1], "data") == 0);
		tap_check(strcmp(c.records[2].fields[2], "factory") == 0);
		csv_release(&c);
		tap_done("three plain records parse correctly");
	}

	/* Whitespace around fields is trimmed. */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(2, "  nvs ,\tdata\t,  nvs  \n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 1);
		tap_check(strcmp(c.records[0].fields[0], "nvs") == 0);
		tap_check(strcmp(c.records[0].fields[1], "data") == 0);
		tap_check(strcmp(c.records[0].fields[2], "nvs") == 0);
		csv_release(&c);
		tap_done("leading/trailing whitespace is trimmed");
	}

	/* Blank lines and '#' comment lines are skipped. */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(3, "# this is a header\n"
						"\n"
						"nvs,data,nvs\n"
						"   \n"
						"# another comment\n"
						"factory,app,factory\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 2);
		tap_check(strcmp(c.records[0].fields[0], "nvs") == 0);
		tap_check(strcmp(c.records[1].fields[0], "factory") == 0);
		csv_release(&c);
		tap_done("blank lines and '#' comments are skipped");
	}

	/* Inline comments ('#' to end of line) are stripped. */
	{
		struct csv c = CSV_INIT;
		const char *path =
		    write_csv(4, "nvs,data,nvs  # this is the nvs partition\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 1);
		tap_check(c.records[0].nr_fields == 3);
		tap_check(strcmp(c.records[0].fields[2], "nvs") == 0);
		csv_release(&c);
		tap_done("inline '#' comment is stripped");
	}

	/* Source line numbers are 1-based and survive blank/comment lines. */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(5, "# comment on line 1\n"
						"\n"
						"a,b,c\n"
						"\n"
						"d,e,f\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 2);
		tap_check(c.records[0].lineno == 3);
		tap_check(c.records[1].lineno == 5);
		csv_release(&c);
		tap_done("lineno records absolute 1-based source line");
	}

	/* Trailing comma yields an empty trailing field (IDF dialect). */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(6, "nvs,data,nvs,\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 1);
		tap_check(c.records[0].nr_fields == 4);
		tap_check(strcmp(c.records[0].fields[3], "") == 0);
		csv_release(&c);
		tap_done("trailing comma gives an empty trailing field");
	}

	/* Multiple csv_load calls accumulate. */
	{
		struct csv c = CSV_INIT;
		const char *a = write_csv(7, "one,two\n");
		const char *b = write_csv(8, "three,four\n");

		tap_check(csv_load(&c, a) == 0);
		tap_check(csv_load(&c, b) == 0);
		tap_check(c.nr == 2);
		tap_check(strcmp(c.records[0].fields[0], "one") == 0);
		tap_check(strcmp(c.records[1].fields[0], "three") == 0);
		csv_release(&c);
		tap_done("csv_load accumulates across calls");
	}

	/* Quoted field preserves interior commas. */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(9, "\"a,b\",\"c\"\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 1);
		tap_check(c.records[0].nr_fields == 2);
		tap_check(strcmp(c.records[0].fields[0], "a,b") == 0);
		tap_check(strcmp(c.records[0].fields[1], "c") == 0);
		csv_release(&c);
		tap_done("quoted field preserves interior commas");
	}

	/* Escaped quotes: "" inside a quoted field is a literal ". */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(10, "\"say \"\"hi\"\"\"\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 1);
		tap_check(strcmp(c.records[0].fields[0], "say \"hi\"") == 0);
		csv_release(&c);
		tap_done("\"\" inside a quoted field is a literal \"");
	}

	/* '#' inside quotes is literal; '#' outside is a comment. */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(11, "\"foo#bar\",baz  # tail\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(c.nr == 1);
		tap_check(c.records[0].nr_fields == 2);
		tap_check(strcmp(c.records[0].fields[0], "foo#bar") == 0);
		tap_check(strcmp(c.records[0].fields[1], "baz") == 0);
		csv_release(&c);
		tap_done("'#' inside quotes is literal, outside is a comment");
	}

	/* Whitespace inside quotes is preserved verbatim. */
	{
		struct csv c = CSV_INIT;
		const char *path = write_csv(12, "\"  spaced  \",unq\n");

		tap_check(csv_load(&c, path) == 0);
		tap_check(strcmp(c.records[0].fields[0], "  spaced  ") == 0);
		tap_check(strcmp(c.records[0].fields[1], "unq") == 0);
		csv_release(&c);
		tap_done("whitespace inside quotes is preserved");
	}

	/* Writer: csv_serialize always quotes and escapes interior ". */
	{
		struct csv c = CSV_INIT;
		struct csv_record *r = csv_add_record(&c);
		struct sbuf out = SBUF_INIT;

		csv_record_add(r, "foo");
		csv_record_add(r, "a,b");
		csv_record_add(r, "he said \"hi\"");
		csv_serialize(&c, &out);

		tap_check(strcmp(out.buf,
				 "\"foo\",\"a,b\",\"he said \"\"hi\"\"\"\n") ==
			  0);

		csv_release(&c);
		sbuf_release(&out);
		tap_done("csv_serialize always quotes and escapes");
	}

	/* Writer: csv_record_addf formats fields. */
	{
		struct csv c = CSV_INIT;
		struct csv_record *r = csv_add_record(&c);

		csv_record_addf(r, "0x%x", 0x9000);
		csv_record_addf(r, "%s_%d", "ota", 1);

		tap_check(r->nr_fields == 2);
		tap_check(strcmp(r->fields[0], "0x9000") == 0);
		tap_check(strcmp(r->fields[1], "ota_1") == 0);
		csv_release(&c);
		tap_done("csv_record_addf formats fields");
	}

	/* Round-trip: every tricky field survives save → load. */
	{
		struct csv src = CSV_INIT;
		struct csv dst = CSV_INIT;
		struct csv_record *r;

		r = csv_add_record(&src);
		csv_record_add(r, "has,comma");
		csv_record_add(r, "has\"quote");
		csv_record_add(r, "has#hash");

		r = csv_add_record(&src);
		csv_record_add(r, "normal");
		csv_record_add(r, "  spaced field  ");

		tap_check(csv_save(&src, "roundtrip.csv") == 0);
		tap_check(csv_load(&dst, "roundtrip.csv") == 0);

		tap_check(dst.nr == 2);
		tap_check(strcmp(dst.records[0].fields[0], "has,comma") == 0);
		tap_check(strcmp(dst.records[0].fields[1], "has\"quote") == 0);
		tap_check(strcmp(dst.records[0].fields[2], "has#hash") == 0);
		tap_check(strcmp(dst.records[1].fields[0], "normal") == 0);
		tap_check(
		    strcmp(dst.records[1].fields[1], "  spaced field  ") == 0);

		csv_release(&src);
		csv_release(&dst);
		tap_done("save → load round-trip preserves tricky fields");
	}

	/* Missing file returns -1. */
	{
		struct csv c = CSV_INIT;
		tap_check(csv_load(&c, "no_such_file.csv") == -1);
		tap_check(c.nr == 0);
		csv_release(&c);
		tap_done("missing file returns -1");
	}

	return tap_result();
}
