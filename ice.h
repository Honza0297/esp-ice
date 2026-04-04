/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ice.h
 * @brief Project-wide header -- include first in every .c file.
 *
 * Pulls in standard headers, the platform abstraction layer, and
 * all common project modules so that every translation unit starts
 * from a common baseline.
 */
#ifndef ICE_H
#define ICE_H

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "error.h"
#include "http.h"
#include "options.h"
#include "platform.h"
#include "process.h"
#include "sbuf.h"
#include "svec.h"

/* Subcommands (defined in builtin/) */
int cmd_build(int argc, const char **argv);

#endif /* ICE_H */
