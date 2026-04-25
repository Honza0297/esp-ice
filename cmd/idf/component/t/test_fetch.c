/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for cmd/idf/component/fetch.c primitives.
 * Skips the network-bound @c fetch_download -- integration tests in a
 * follow-up phase can exercise that against a local file:// server.
 */
#include "cmd/idf/component/fetch.h"
#include "cmd/idf/component/lockfile.h"
#include "ice.h"
#include "tap.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void build(struct sbuf *out, const char *name)
{
	sbuf_reset(out);
	fetch_build_name(out, name);
}

int main(void)
{
	/* Name normalisation: slash -> double underscore. */
	{
		struct sbuf sb = SBUF_INIT;
		build(&sb, "example/cmp");
		tap_check(!strcmp(sb.buf, "example__cmp"));
		build(&sb, "espressif/cjson");
		tap_check(!strcmp(sb.buf, "espressif__cjson"));
		build(&sb, "idf");
		tap_check(!strcmp(sb.buf, "idf"));
		/* Multi-slash names (not used in practice, but must not drop
		 * segments). */
		build(&sb, "a/b/c");
		tap_check(!strcmp(sb.buf, "a__b__c"));
		build(&sb, "");
		tap_check(!strcmp(sb.buf, ""));
		sbuf_release(&sb);
		tap_done("fetch_build_name: slash -> __");
	}

	/* sha256: known vectors.  "abc" -> ba7816bf8f01cfea4141... */
	{
		const char *path = "abc.bin";
		FILE *fp = fopen(path, "wb");
		tap_check(fp != NULL);
		fputs("abc", fp);
		fclose(fp);

		char hex[65];
		tap_check(fetch_compute_sha256(path, hex) == 0);
		tap_check(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a"
				       "396177a9cb410ff61f20015ad"));
		tap_done("fetch_compute_sha256: classic 'abc' vector");
	}

	/* sha256: empty file -> e3b0c44298fc1c149afbf4c8996fb92427ae41e4... */
	{
		const char *path = "empty.bin";
		FILE *fp = fopen(path, "wb");
		tap_check(fp != NULL);
		fclose(fp);

		char hex[65];
		tap_check(fetch_compute_sha256(path, hex) == 0);
		tap_check(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e"
				       "4649b934ca495991b7852b855"));
		tap_done("fetch_compute_sha256: empty file");
	}

	/* Missing file -> error. */
	{
		char hex[65];
		tap_check(fetch_compute_sha256("no_such_file", hex) == -1);
		tap_done("fetch_compute_sha256: missing file returns -1");
	}

	/* Verify: matching digest succeeds, case-insensitive. */
	{
		const char *path =
		    "abc.bin"; /* still around from earlier test */
		tap_check(fetch_verify_sha256(
			      path, "ba7816bf8f01cfea414140de5dae2223b00361a396"
				    "177a9cb410ff61f20015ad") == 0);
		tap_check(fetch_verify_sha256(
			      path, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396"
				    "177A9CB410FF61F20015AD") == 0);
		/* Mismatch. */
		tap_check(fetch_verify_sha256(
			      path, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefde"
				    "adbeefdeadbeefdeadbeef") == -1);
		/* NULL expected. */
		tap_check(fetch_verify_sha256(path, NULL) == -1);
		tap_done("fetch_verify_sha256: match, case, mismatch, NULL");
	}

	/* fetch_component: IDF / LOCAL sources are no-ops -- nothing
	 * downloaded, nothing extracted, no directory created. */
	{
		struct lockfile_entry e_idf = {
		    .name = (char *)"idf",
		    .src_type = LOCKFILE_SRC_IDF,
		};
		struct lockfile_entry e_local = {
		    .name = (char *)"helper",
		    .src_type = LOCKFILE_SRC_LOCAL,
		};
		struct stat st;

		tap_check(fetch_component(&e_idf, "noop_dir") == 0);
		tap_check(fetch_component(&e_local, "noop_dir") == 0);
		tap_check(stat("noop_dir", &st) != 0);
		tap_done("fetch_component: IDF/LOCAL are no-ops");
	}

	/* fetch_component: UNKNOWN / NULL inputs reject cleanly. */
	{
		struct lockfile_entry e_unknown = {
		    .name = (char *)"x",
		    .src_type = LOCKFILE_SRC_UNKNOWN,
		};
		tap_check(fetch_component(&e_unknown, "noop_dir") == -1);
		tap_check(fetch_component(NULL, "noop_dir") == -1);
		{
			struct lockfile_entry e = {.name = NULL};
			tap_check(fetch_component(&e, "noop_dir") == -1);
		}
		tap_done("fetch_component: UNKNOWN / NULL rejected");
	}

	return tap_result();
}
