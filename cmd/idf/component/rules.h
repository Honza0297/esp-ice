/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/idf/component/rules.h
 * @brief Evaluator for `if:` clauses in idf_component.yml dependencies.
 *
 * Manifest deps can carry an @c if: expression (or a list of
 * @c rules: with embedded @c if:) that gates whether the dep
 * applies.  The expression syntax matches the Python tool's
 * @c if_parser.py:
 *
 *   <left> <op> <right>            # atomic clause
 *   <clause> && <clause>            # conjunction
 *   <clause> || <clause>            # disjunction
 *   ( <expr> )                      # grouping
 *
 * Left values:
 *   - @c target            -- chip name string ("esp32", "linux", ...)
 *   - @c idf_version       -- IDF version, compared as SemVer
 *   - @c CONFIG_<NAME>     -- looked up in @c sdkconfig.json
 *
 * Right values: bare identifiers, quoted strings, numbers, semver
 * versions, or @c [a, b, c] lists for the @c in / @c not in operators.
 *
 * Operators:
 *   ==  !=  <  <=  >  >=  in  not in
 *
 * @c idf_version comparisons treat the right side as a SemVer
 * constraint piece (e.g. @c idf_version >= 5.0 = match against
 * @c >=5.0); other left values are compared as strings or numbers.
 */
#ifndef CMD_IDF_COMPONENT_RULES_H
#define CMD_IDF_COMPONENT_RULES_H

struct json_value;

struct rules_ctx {
	const char *target;	 /**< chip name; NULL → "unknown" */
	const char *idf_version; /**< version string; NULL → "0.0.0" */
	const struct json_value *sdkconfig;
};

/**
 * @brief Evaluate an if-clause expression.
 *
 * @return  1 if the expression is true,
 *          0 if false,
 *         -1 on parse or evaluation error.
 */
int rules_eval(const char *expr, const struct rules_ctx *ctx);

#endif /* CMD_IDF_COMPONENT_RULES_H */
