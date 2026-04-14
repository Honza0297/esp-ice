/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for json.c -- the small in-house JSON DOM.
 */
#include "ice.h"
#include "tap.h"

int main(void)
{
	/* Parse and inspect a small JSON object. */
	{
		const char *src = "{\"target\":\"esp32s3\",\"size\":4096}";
		struct json_value *root = json_parse(src, strlen(src));
		const char *target;
		double size;

		tap_check(root != NULL);
		tap_check(json_type(root) == JSON_OBJECT);

		target = json_as_string(json_get(root, "target"));
		tap_check(target != NULL);
		tap_check(strcmp(target, "esp32s3") == 0);

		size = json_as_number(json_get(root, "size"));
		tap_check(size == 4096.0);

		/* Missing key: accessors return NULL / 0 / 0.0, not crash. */
		tap_check(json_get(root, "missing") == NULL);
		tap_check(json_as_string(json_get(root, "missing")) == NULL);
		tap_check(json_as_number(json_get(root, "missing")) == 0.0);

		json_free(root);
		tap_done("parse object with string and number members");
	}

	/* Arrays: size and indexed access. */
	{
		const char *src = "[10, 20, 30]";
		struct json_value *root = json_parse(src, strlen(src));

		tap_check(json_type(root) == JSON_ARRAY);
		tap_check(json_array_size(root) == 3);
		tap_check(json_as_number(json_array_at(root, 0)) == 10.0);
		tap_check(json_as_number(json_array_at(root, 2)) == 30.0);
		tap_check(json_array_at(root, 99) == NULL);

		json_free(root);
		tap_done("parse array, indexed access, out-of-range NULL");
	}

	/* Nested structures: object containing an array of objects. */
	{
		const char *src = "{\"items\":[{\"k\":\"a\"},{\"k\":\"b\"}]}";
		struct json_value *root = json_parse(src, strlen(src));
		struct json_value *items = json_get(root, "items");
		struct json_value *first;

		tap_check(json_array_size(items) == 2);
		first = json_array_at(items, 0);
		tap_check(strcmp(json_as_string(json_get(first, "k")), "a") ==
			  0);

		json_free(root);
		tap_done("nested object/array traversal");
	}

	/* Booleans, null, and the corresponding accessors. */
	{
		const char *src = "{\"yes\":true,\"no\":false,\"nil\":null}";
		struct json_value *root = json_parse(src, strlen(src));

		tap_check(json_as_bool(json_get(root, "yes")) == 1);
		tap_check(json_as_bool(json_get(root, "no")) == 0);
		tap_check(json_type(json_get(root, "nil")) == JSON_NULL);

		json_free(root);
		tap_done("bool / null types and accessors");
	}

	/* Build, serialize, and verify the round-trip. */
	{
		struct json_value *o = json_new_object();
		struct sbuf out = SBUF_INIT;

		json_set(o, "target", json_new_string("esp32"));
		json_set(o, "size", json_new_number(42));
		json_serialize(o, &out);
		/* Compact form: no whitespace; member order preserved. */
		tap_check(
		    strcmp(out.buf, "{\"target\":\"esp32\",\"size\":42}") == 0);

		json_free(o);
		sbuf_release(&out);
		tap_done("build object and round-trip via json_serialize");
	}

	/* json_set replaces an existing member with the same key. */
	{
		struct json_value *o = json_new_object();
		json_set(o, "k", json_new_number(1));
		json_set(o, "k", json_new_number(2));
		tap_check(json_as_number(json_get(o, "k")) == 2.0);
		json_free(o);
		tap_done("json_set replaces existing same-key member");
	}

	/* Invalid input returns NULL rather than crashing. */
	{
		const char *bad = "{not json}";
		tap_check(json_parse(bad, strlen(bad)) == NULL);
		tap_done("invalid input returns NULL");
	}

	return tap_result();
}
