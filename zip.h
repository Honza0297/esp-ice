/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file zip.h
 * @brief Minimal ZIP archive extractor.
 *
 * Reads PKZIP archives sized to fit in 32-bit offsets (ZIP64 is
 * rejected).  Supports the two compression methods real-world
 * component archives use:
 *
 *   - @c STORE   (method 0) -- copied verbatim
 *   - @c DEFLATE (method 8) -- decompressed via zlib's raw inflate
 *
 * Entry filenames containing @c ".." segments or a leading @c "/"
 * are rejected to prevent archives from writing outside the
 * destination directory.  Per-entry @c CRC-32 is verified against
 * the value recorded in the central directory.
 *
 * Usage:
 *
 *   if (zip_extract_all("pkg.zip", "managed_components/example__cmp") < 0)
 *           die("extraction failed");
 */
#ifndef ZIP_H
#define ZIP_H

#include <stddef.h>

/**
 * @brief Extract every entry of @p zip_path into @p dest_dir.
 *
 * @p dest_dir and any intermediate directories are created as needed.
 * The archive is read fully into memory -- intended for registry
 * component archives (a few MB at most), not general-purpose use.
 *
 * @return 0 on success, -1 on any parse, I/O, or decompression error.
 */
int zip_extract_all(const char *zip_path, const char *dest_dir);

#endif /* ZIP_H */
