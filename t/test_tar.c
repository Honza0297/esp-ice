/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for tar.c.  The driver (t/0010-tar.t) builds small
 * archives with the system `tar` and runs this binary to exercise the
 * three compression paths: plain .tar, .tar.gz (zlib), .tar.xz (XZ
 * Embedded).  One adversarial archive verifies the tar-slip defense.
 *
 * We verify structure + file contents only; symlink target inspection
 * would need POSIX-only calls and is covered by the manual diff-check
 * against real Espressif toolchain tarballs.
 */

#include "ice.h"
#include "tap.h"
#include "tar.h"

static int file_eq(const char *path, const char *want)
{
	FILE *fp = fopen(path, "rb");
	char buf[256];
	if (!fp)
		return 0;
	size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	buf[n] = 0;
	return strcmp(buf, want) == 0;
}

static void exercise(const char *archive, const char *outdir)
{
	mkdir(outdir, 0755);
	tap_check(tar_extract(archive, outdir) == 0);

	struct sbuf p = SBUF_INIT;

	sbuf_addf(&p, "%s/hello.txt", outdir);
	tap_check(file_eq(p.buf, "hello\n"));
	sbuf_reset(&p);

	sbuf_addf(&p, "%s/sub/nested.txt", outdir);
	tap_check(file_eq(p.buf, "nested\n"));

	sbuf_release(&p);
}

int main(void)
{
	exercise("test.tar", "out-plain");
	tap_done("plain .tar extracts correctly");

	exercise("test.tar.gz", "out-gz");
	tap_done(".tar.gz extracts correctly (zlib)");

	exercise("test.tar.xz", "out-xz");
	tap_done(".tar.xz extracts correctly (XZ Embedded)");

	/* Tar-slip: archive contains an entry named "../escape".
	 * Extraction should succeed (unsafe entry skipped) and no file
	 * should appear outside the extraction root. */
	mkdir("out-slip", 0755);
	tap_check(tar_extract("slip.tar", "out-slip") == 0);
	tap_check(access("out-slip/../escape", F_OK) != 0);
	tap_done("../ entry is refused, nothing escapes outdir");

	return tap_result();
}
