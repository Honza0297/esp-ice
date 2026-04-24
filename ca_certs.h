/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ca_certs.h
 * @brief Apply the bundled CA trust store to a libcurl handle.
 *
 * `ice` ships the Mozilla CA bundle (from vendor/cacert/) compiled
 * into the binary.  For every HTTPS request we override libcurl's
 * notion of the trust store with this bundle, so TLS verification
 * does not depend on the host system having a current
 * ca-certificates package installed.
 */
#ifndef CA_CERTS_H
#define CA_CERTS_H

#include <curl/curl.h>

/**
 * @brief Configure @p curl to verify peers against the bundled CA store.
 *
 * Sets @c CURLOPT_CAINFO_BLOB to the embedded Mozilla bundle.  Call
 * once per easy handle, after the URL and other defaults have been
 * set; safe to call on any backend that supports CAINFO_BLOB
 * (OpenSSL, mbedTLS, wolfSSL, BearSSL, GnuTLS, Rustls — i.e. every
 * backend ice builds against).
 */
void ca_certs_apply(CURL *curl);

#endif /* CA_CERTS_H */
