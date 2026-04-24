/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/update/update.c
 * @brief `ice update` -- self-update to the latest release.
 *
 * Queries the GitHub Releases API, downloads the tarball for the
 * current @c ICE_PLATFORM_OS / @c ICE_PLATFORM_ARCH, extracts it to a
 * scratch directory under @c ice_home(), and replaces the running
 * binary via @c write_file_atomic().  The tmp-then-rename inside
 * @c write_file_atomic is backed by POSIX @c rename(2) and @c rename_w
 * (MOVEFILE_REPLACE_EXISTING) on Windows -- both safe to run against
 * the image of the process doing the renaming.
 */
#include "ice.h"

#include "tar.h"

/* clang-format off */
static const struct cmd_manual update_manual = {
	.name = "ice update",
	.summary = "download and install the latest ice release",

	.description =
	H_PARA("Resolve the latest tag from the GitHub Releases API for "
	       "@b{fhrbata/esp-ice}, download the release tarball for the "
	       "current platform, and replace the running @b{ice} binary "
	       "in place.")
	H_PARA("The replacement is atomic: the new bytes are written to "
	       "@b{<ice>.tmp} next to the running binary, then renamed over "
	       "it.  A crash mid-download or mid-extract leaves the "
	       "installed copy untouched."),

	.examples =
	H_EXAMPLE("ice update")
	H_EXAMPLE("ice update --check"),

	.extras =
	H_SECTION("SEE ALSO")
	H_ITEM("install.sh / install.ps1",
	       "Standalone installers used for the initial install; "
	       "functionally equivalent to this command on a fresh system."),
};
/* clang-format on */

static int opt_check;

static const struct option cmd_update_opts[] = {
    OPT_BOOL(0, "check", &opt_check,
	     "only report the current and latest versions; do not install"),
    OPT_END(),
};

int cmd_update(int argc, const char **argv);

const struct cmd_desc cmd_update_desc = {
    .name = "update",
    .fn = cmd_update,
    .opts = cmd_update_opts,
    .manual = &update_manual,
};

#define ICE_REPO "fhrbata/esp-ice"

/* Resolve the latest release tag via GitHub's releases API. */
static void fetch_latest_version(char *out, size_t out_len)
{
	struct sbuf url = SBUF_INIT;
	struct sbuf body = SBUF_INIT;
	struct json_value *rel;
	const char *tag;
	int status;

	sbuf_addf(&url, "https://api.github.com/repos/%s/releases/latest",
		  ICE_REPO);

	status = http_get(url.buf, &body);
	if (status != 200)
		die("GitHub API request failed (HTTP %d): %s", status, url.buf);

	rel = json_parse(body.buf, body.len);
	if (!rel)
		die("failed to parse release JSON from %s", url.buf);

	tag = json_as_string(json_get(rel, "tag_name"));
	if (!tag)
		die("release JSON has no 'tag_name' field");

	if (tag[0] == 'v')
		tag++;
	snprintf(out, out_len, "%s", tag);

	json_free(rel);
	sbuf_release(&body);
	sbuf_release(&url);
}

int cmd_update(int argc, const char **argv)
{
	char latest[64];
	const char *exe;
	struct sbuf workdir = SBUF_INIT;
	struct sbuf tarball = SBUF_INIT;
	struct sbuf extracted = SBUF_INIT;
	struct sbuf url = SBUF_INIT;
	struct sbuf binary = SBUF_INIT;
	struct sbuf contents = SBUF_INIT;
	struct sbuf lock = SBUF_INIT;

	argc = parse_options(argc, argv, &cmd_update_desc);
	if (argc > 0)
		die("too many arguments");

	fprintf(stderr, "Resolving latest version...\n");
	fetch_latest_version(latest, sizeof(latest));
	fprintf(stderr, "Current: @b{%s}  Latest: @b{%s}\n", VERSION, latest);

	if (!strcmp(VERSION, latest)) {
		fprintf(stderr, "@g{ice %s} - already up to date\n", VERSION);
		return 0;
	}

	if (opt_check)
		return 0;

	/*
	 * Release assets for Windows are .zip; the bundled tar extractor
	 * handles tar / tar.gz / tar.xz only, so self-update would need a
	 * zip reader we don't have today.  Point users at the re-installable
	 * PowerShell installer instead.
	 */
	if (!strcmp(ICE_PLATFORM_OS, "win"))
		die("'ice update' is not yet supported on Windows (release "
		    "assets are .zip); re-run install.ps1 to upgrade");

	exe = process_exe();
	if (!exe)
		die("cannot determine the path of the running binary");

	/*
	 * Lock next to the running binary so two concurrent `ice update`
	 * invocations can't race each other into the same destination.
	 * atexit cleanup inside lock_acquire removes the file if we die
	 * before reaching lock_release.
	 */
	sbuf_addf(&lock, "%s.update.lock", exe);
	if (lock_acquire(lock.buf, 0) < 0) {
		if (errno == EEXIST)
			die("another 'ice update' is running (lock: %s)",
			    lock.buf);
		die_errno("cannot acquire %s", lock.buf);
	}

	/* Scratch space under ICE_HOME. */
	sbuf_addf(&workdir, "%s/update", ice_home());
	if (is_directory(workdir.buf))
		rmtree(workdir.buf, 0);
	else
		unlink(workdir.buf);
	if (mkdirp(workdir.buf) < 0)
		die_errno("mkdir %s", workdir.buf);

	/* Download. */
	sbuf_addf(&url,
		  "https://github.com/%s/releases/download/v%s/"
		  "ice-%s-%s-%s.tar.gz",
		  ICE_REPO, latest, latest, ICE_PLATFORM_OS, ICE_PLATFORM_ARCH);
	sbuf_addf(&tarball, "%s/ice-%s-%s-%s.tar.gz", workdir.buf, latest,
		  ICE_PLATFORM_OS, ICE_PLATFORM_ARCH);

	fprintf(stderr, "Downloading %s\n", url.buf);
	if (http_download(url.buf, tarball.buf, http_default_progress, NULL) <
	    0) {
		fputc('\n', stderr);
		die("download failed: %s", url.buf);
	}
	fputc('\n', stderr);

	/* Extract. */
	sbuf_addf(&extracted, "%s/extract", workdir.buf);
	if (mkdirp(extracted.buf) < 0)
		die_errno("mkdir %s", extracted.buf);
	fprintf(stderr, "Extracting...\n");
	if (tar_extract(tarball.buf, extracted.buf) < 0)
		die("extraction failed");

	/* Read the extracted binary into memory. */
	sbuf_addf(&binary, "%s/ice-%s/bin/ice", extracted.buf, latest);
	if (sbuf_read_file(&contents, binary.buf) < 0)
		die_errno("cannot read extracted binary: %s", binary.buf);

	/* Self-replace. */
	if (write_file_atomic(exe, contents.buf, contents.len) < 0)
		die_errno("cannot write %s", exe);
	if (chmod(exe, 0755) < 0)
		warn_errno("chmod 0755 %s", exe);

	fprintf(stderr, "@g{ice %s} installed to %s\n", latest, exe);

	rmtree(workdir.buf, 0);
	rmdir(workdir.buf);
	lock_release(lock.buf);

	sbuf_release(&workdir);
	sbuf_release(&tarball);
	sbuf_release(&extracted);
	sbuf_release(&url);
	sbuf_release(&binary);
	sbuf_release(&contents);
	sbuf_release(&lock);
	return 0;
}
