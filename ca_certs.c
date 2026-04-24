/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ca_certs.c
 * @brief Apply the bundled Mozilla CA store to libcurl handles.
 *
 * The bundle itself lives in vendor/cacert/ — cacert.pem is the
 * upstream PEM, ca_bundle.c is the same data as a compiled-in byte
 * array.  This module is the integration glue: it wraps the array in
 * a curl_blob and installs it via CURLOPT_CAINFO_BLOB.
 *
 * CAINFO_BLOB *replaces* the system trust store for that handle.  We
 * accept the trade-off in exchange for one curl API that works on
 * every TLS backend ice builds against and TLS that behaves
 * identically regardless of the host's ca-certificates state.
 */
#include "ca_certs.h"

/* Defined in vendor/cacert/ca_bundle.c (auto-generated from
 * vendor/cacert/cacert.pem). */
extern const unsigned char ca_bundle_pem[];
extern const size_t ca_bundle_pem_len;

void ca_certs_apply(CURL *curl)
{
	struct curl_blob blob = {
	    .data = (void *)ca_bundle_pem,
	    .len = ca_bundle_pem_len,
	    .flags = CURL_BLOB_NOCOPY,
	};

	curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
}
