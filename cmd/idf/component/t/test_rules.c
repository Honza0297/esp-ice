/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for cmd/idf/component/rules.c -- the if-clause
 * evaluator used to gate conditional dependencies in
 * idf_component.yml.
 */
#include "cmd/idf/component/rules.h"
#include "ice.h"
#include "json.h"
#include "tap.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
	const struct rules_ctx esp32_v6 = {
	    .target = "esp32",
	    .idf_version = "6.1.0",
	};
	const struct rules_ctx linux_v5 = {
	    .target = "linux",
	    .idf_version = "5.0.0",
	};

	/* target equality: the canonical real-world case. */
	{
		tap_check(rules_eval("target == esp32", &esp32_v6) == 1);
		tap_check(rules_eval("target != linux", &esp32_v6) == 1);
		tap_check(rules_eval("target != linux", &linux_v5) == 0);
		tap_check(rules_eval("target == \"esp32\"", &esp32_v6) == 1);
		tap_done("target ==/!= comparisons");
	}

	/* target with order operators is rejected by the Python tool;
	 * we mirror that. */
	{
		tap_check(rules_eval("target < esp32", &esp32_v6) == -1);
		tap_check(rules_eval("target >= esp32", &esp32_v6) == -1);
		tap_done("target rejects order operators");
	}

	/* idf_version uses SemVer-spec semantics. */
	{
		tap_check(rules_eval("idf_version >= 5.0", &esp32_v6) == 1);
		tap_check(rules_eval("idf_version < 5.0", &esp32_v6) == 0);
		tap_check(rules_eval("idf_version >= 6.1.0", &esp32_v6) == 1);
		tap_check(rules_eval("idf_version >= 7.0.0", &esp32_v6) == 0);
		tap_check(rules_eval("idf_version == 6.1.0", &esp32_v6) == 1);
		tap_check(rules_eval("idf_version != 6.1.0", &esp32_v6) == 0);
		tap_check(rules_eval("idf_version >= 5.0", &linux_v5) == 1);
		tap_done("idf_version SemVer comparisons");
	}

	/* in / not in over a target list. */
	{
		tap_check(rules_eval("target in [esp32, esp32s3]", &esp32_v6) ==
			  1);
		tap_check(
		    rules_eval("target in [esp32s2, esp32s3]", &esp32_v6) == 0);
		tap_check(rules_eval("target not in [linux, esp32p4]",
				     &esp32_v6) == 1);
		tap_check(
		    rules_eval("target not in [linux, esp32]", &esp32_v6) == 0);
		tap_done("in / not in with target lists");
	}

	/* AND combinator, including short-circuit on left=false. */
	{
		tap_check(rules_eval("target == esp32 && idf_version >= 5.0",
				     &esp32_v6) == 1);
		tap_check(rules_eval("target == linux && idf_version >= 5.0",
				     &esp32_v6) == 0);
		tap_done("&& combinator");
	}

	/* OR combinator. */
	{
		tap_check(rules_eval("target == esp32 || target == esp32s3",
				     &esp32_v6) == 1);
		tap_check(rules_eval("target == esp32s2 || target == esp32s3",
				     &esp32_v6) == 0);
		tap_check(rules_eval("target == linux || idf_version >= 5.0",
				     &esp32_v6) == 1);
		tap_done("|| combinator");
	}

	/* Parenthesised grouping. */
	{
		tap_check(
		    rules_eval("(target == esp32 || target == esp32s3) && "
			       "idf_version >= 5.0",
			       &esp32_v6) == 1);
		tap_check(
		    rules_eval("(target == linux || target == esp32s3) && "
			       "idf_version >= 5.0",
			       &esp32_v6) == 0);
		tap_done("parenthesised grouping");
	}

	/* CONFIG_* lookup against an sdkconfig.json document. */
	{
		const char *cfg_json = "{ \"CONFIG_FEATURE_X\": \"y\","
				       "  \"CONFIG_NUMBER\": 42,"
				       "  \"CONFIG_NAME\": \"hello\" }";
		struct json_value *cfg = json_parse(cfg_json, strlen(cfg_json));
		tap_check(cfg != NULL);
		struct rules_ctx ctx = {
		    .target = "esp32",
		    .idf_version = "6.1.0",
		    .sdkconfig = cfg,
		};
		tap_check(rules_eval("CONFIG_FEATURE_X == y", &ctx) == 1);
		tap_check(rules_eval("CONFIG_FEATURE_X != n", &ctx) == 1);
		tap_check(rules_eval("CONFIG_NUMBER == 42", &ctx) == 1);
		tap_check(rules_eval("CONFIG_NUMBER != 0", &ctx) == 1);
		tap_check(rules_eval("CONFIG_NAME == \"hello\"", &ctx) == 1);
		/* Missing CONFIG_* is an error -- caller's responsibility
		 * to default the value if "missing means false" is desired. */
		tap_check(rules_eval("CONFIG_MISSING == y", &ctx) == -1);
		json_free(cfg);
		tap_done("CONFIG_* lookup from sdkconfig.json");
	}

	/* Malformed expressions return -1. */
	{
		tap_check(rules_eval("", &esp32_v6) == -1);
		tap_check(rules_eval("target", &esp32_v6) == -1);
		tap_check(rules_eval("target ==", &esp32_v6) == -1);
		tap_check(rules_eval("target == esp32 &&", &esp32_v6) == -1);
		tap_check(rules_eval("(target == esp32", &esp32_v6) ==
			  -1); /* unclosed */
		tap_check(rules_eval("target in esp32", &esp32_v6) ==
			  -1); /* missing [ */
		tap_check(rules_eval("target in [esp32", &esp32_v6) ==
			  -1); /* missing ] */
		tap_done("malformed expressions return -1");
	}

	return tap_result();
}
