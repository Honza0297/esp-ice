/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file md5.h
 * @brief Portable MD5 (RFC 1321).  No external library required.
 */
#ifndef MD5_H
#define MD5_H

#include <stddef.h>
#include <stdint.h>

struct md5_ctx {
	uint32_t state[4];
	uint32_t count[2]; /* bit count, low then high */
	uint8_t buf[64];
};

void md5_init(struct md5_ctx *ctx);
void md5_update(struct md5_ctx *ctx, const uint8_t *data, size_t len);
void md5_final(struct md5_ctx *ctx, uint8_t digest[16]);

#endif /* MD5_H */
