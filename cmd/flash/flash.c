/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/flash/flash.c
 * @brief The "ice flash" subcommand -- porcelain wrapper around
 * `ice target flash`.
 *
 * Resolves the serial port (auto-detection or config), chip target,
 * baud rate, and the list of images to flash from the project's
 * flasher_args.json, then delegates to cmd_target_flash() which
 * carries out the actual connection and write.
 */
#include "cmake.h"
#include "esf_port.h"
#include "ice.h"

/* Plumbing entry point declared in cmd/target/flash.c. */
int cmd_target_flash(int argc, const char **argv);

static const char *opt_port;
static int opt_baud = 460800;

/* clang-format off */
static const struct option cmd_flash_opts[] = {
	OPT_POSITIONAL_OPT("name", complete_profile_names),
	OPT_STRING_CFG('p', "port", &opt_port, "dev",
		       "serial.port", "ESPPORT",
		       "serial port device", NULL, NULL),
	OPT_INT_CFG('b', "baud", &opt_baud, "rate",
		    "serial.baud", "ESPBAUD",
		    "baud rate", NULL, NULL),
	OPT_END(),
};

static const struct cmd_manual manual = {
	.name = "ice flash",
	.summary = "flash firmware to the device",
	.description =
	H_PARA("Programs the compiled firmware -- application, bootloader, "
	       "partition table -- directly to a connected ESP device over "
	       "serial using the ESP ROM bootloader protocol.")
	H_PARA("Partition offsets and binary paths are read from "
	       "@b{flasher_args.json} in the build directory, which is "
	       "produced automatically by the IDF cmake build.  The target "
	       "chip is also read from that file and used to auto-detect the "
	       "correct port when none is specified.  The baud rate comes "
	       "from @b{serial.baud} in config; the legacy @b{ESPPORT} / "
	       "@b{ESPBAUD} environment variables are mapped to the same "
	       "keys at env scope."),

	.examples =
	H_EXAMPLE("ice flash")
	H_EXAMPLE("ice flash production")
	H_EXAMPLE("ice flash --port /dev/ttyUSB0")
	H_EXAMPLE("ice flash s3 --port /dev/ttyACM0")
	H_EXAMPLE("ice flash --port /dev/ttyUSB0 --baud 921600")
	H_EXAMPLE("ice config serial.port /dev/ttyUSB0 && ice flash")
	H_EXAMPLE("ESPPORT=/dev/ttyUSB1 ice flash"),

	.extras =
	H_SECTION("CONFIG")
	H_ITEM("serial.port",
	       "Serial device path (@b{/dev/ttyUSB0}, @b{COM3}, ...).")
	H_ITEM("serial.baud",
	       "Flasher baud rate (e.g. @b{115200}, @b{460800}).  "
	       "Connection always starts at 115200; this rate is negotiated "
	       "after the ROM handshake.")

	H_SECTION("ENVIRONMENT")
	H_ITEM("ESPPORT",
	       "Alias for @b{serial.port} (env scope).")
	H_ITEM("ESPBAUD",
	       "Alias for @b{serial.baud} (env scope).")

	H_SECTION("SEE ALSO")
	H_ITEM("ice build",
	       "Build the firmware before flashing.")
	H_ITEM("ice target flash",
	       "Plumbing: flash with explicit port and file list.")
	H_ITEM("ice cmake erase-flash",
	       "Wipe flash before reprogramming."),
};
/* clang-format on */

const struct cmd_desc cmd_flash_desc = {
    .name = "flash",
    .fn = cmd_flash,
    .opts = cmd_flash_opts,
    .manual = &manual,
};

int cmd_flash(int argc, const char **argv)
{
	argc = parse_options(argc, argv, &cmd_flash_desc);
	if (argc > 1)
		die("too many arguments");

	const char *name = argc >= 1 ? argv[0] : "default";

	load_profile(name);
	require_project_initialized();

	unsigned baud = (unsigned)opt_baud;

	const char *build_dir = config_get("project.build-dir");
	if (!build_dir)
		build_dir = "build";

	/* ---- locate and parse flasher_args.json ---- */
	struct sbuf args_path = SBUF_INIT;
	sbuf_addf(&args_path, "%s/flasher_args.json", build_dir);

	struct sbuf args_buf = SBUF_INIT;
	if (sbuf_read_file(&args_buf, args_path.buf) < 0) {
		fprintf(
		    stderr,
		    "ice flash: cannot read %s\n"
		    "  Run 'ice build' first to generate build artifacts.\n",
		    args_path.buf);
		sbuf_release(&args_path);
		return 1;
	}
	sbuf_release(&args_path);

	struct json_value *root = json_parse(args_buf.buf, args_buf.len);
	sbuf_release(&args_buf);
	if (!root) {
		fprintf(stderr,
			"ice flash: failed to parse flasher_args.json\n");
		return 1;
	}

	struct json_value *files = json_get(root, "flash_files");
	if (!files || files->type != JSON_OBJECT || files->u.object.nr == 0) {
		fprintf(stderr, "ice flash: 'flash_files' not found in "
				"flasher_args.json\n");
		json_free(root);
		return 1;
	}

	/* ---- resolve target chip ---- */
	target_chip_t required_chip = esf_chip_from_flasher_args(build_dir);

	/* ---- resolve serial port ---- */
	const char *port_path = opt_port;
	char *autoport = NULL;

	if (!port_path) {
		if (required_chip != ESP_UNKNOWN_CHIP)
			printf("Scanning for @b{%s}...\n",
			       esf_chip_name(required_chip));
		else
			printf("Scanning for ESP device...\n");
		fflush(stdout);

		autoport = esf_find_esp_port(required_chip);
		if (!autoport) {
			fprintf(stderr,
				"ice flash: no matching device found.\n"
				"  Use --port to specify a port explicitly.\n");
			json_free(root);
			return 1;
		}
		port_path = autoport;
	}

	/* ---- build argv for ice target flash ---- */
	int n_files = files->u.object.nr;

	/*
	 * Maximum argv size: "ice target flash" + --port <p> + --chip <c> +
	 * --baud <b> + n_files entries + NULL sentinel.
	 */
	int max_argc = 8 + n_files;
	const char **flash_argv = malloc((size_t)(max_argc + 1) * sizeof(*flash_argv));
	char **file_entries = malloc((size_t)n_files * sizeof(*file_entries));
	if (!flash_argv || !file_entries)
		die_errno("malloc");

	int fa = 0;
	flash_argv[fa++] = "ice target flash";
	flash_argv[fa++] = "--port";
	flash_argv[fa++] = port_path;

	const char *chip_str = json_as_string(json_get(
	    json_get(root, "extra_esptool_args"), "chip"));
	if (chip_str) {
		flash_argv[fa++] = "--chip";
		flash_argv[fa++] = chip_str;
	}

	char baud_str[32];
	snprintf(baud_str, sizeof(baud_str), "%u", baud);
	flash_argv[fa++] = "--baud";
	flash_argv[fa++] = baud_str;

	int n_entries = 0;
	for (int i = 0; i < n_files; i++) {
		const char *offset_str = files->u.object.members[i].key;
		const char *rel_path =
		    json_as_string(files->u.object.members[i].value);

		if (!rel_path) {
			fprintf(stderr,
				"ice flash: bad entry in flash_files\n");
			json_free(root);
			free(autoport);
			for (int j = 0; j < n_entries; j++)
				free(file_entries[j]);
			free(file_entries);
			free(flash_argv);
			return 1;
		}

		struct sbuf entry = SBUF_INIT;
		sbuf_addf(&entry, "%s=%s/%s", offset_str, build_dir, rel_path);
		file_entries[n_entries] = entry.buf; /* transfer ownership */
		flash_argv[fa++] = file_entries[n_entries++];
	}
	flash_argv[fa] = NULL;

	json_free(root);

	/* ---- delegate to plumbing ---- */
	int rc = cmd_target_flash(fa, flash_argv);

	free(autoport);
	free(flash_argv);
	for (int i = 0; i < n_entries; i++)
		free(file_entries[i]);
	free(file_entries);

	return rc;
}
