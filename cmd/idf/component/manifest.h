/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/idf/component/manifest.h
 * @brief idf_component.yml parser.
 *
 * Parses the fields @c ice idf component actually needs:
 *
 *   version: "2.0.0"                # optional, component's own version
 *   description: "..."              # optional, informational
 *   targets: ["esp32", "esp32s3"]   # optional, chip filter
 *   dependencies:                   # optional
 *     idf: ">=5.0"                              # simple form
 *     lvgl/lvgl: "9.2.0"                        # simple form
 *     my_component:                             # block form
 *       version: "^1.0.0"
 *       public: true
 *       pre_release: false
 *       if: "CONFIG_FEATURE_X"
 *       rules:
 *         - if: "target != linux"
 *     u8g2:                                     # git source
 *       git: "https://..."
 *       version: "<sha-or-ref>"
 *     helper:                                   # local path
 *       path: "../helper"
 *       service_url: "https://registry..."      # registry override
 *
 * @c ${VAR} expansion in path/git values is not done here -- raw
 * strings are returned and the caller expands them when materialising
 * sources.  Fields not listed above (description-like fields,
 * maintainers, license, ...) are ignored for now.
 */
#ifndef CMD_IDF_COMPONENT_MANIFEST_H
#define CMD_IDF_COMPONENT_MANIFEST_H

#include <stddef.h>

enum manifest_dep_source {
	MANIFEST_SRC_REGISTRY, /**< Default; pulled via service_url or default.
				*/
	MANIFEST_SRC_GIT,      /**< @c git: url given. */
	MANIFEST_SRC_PATH,     /**< @c path: given; local source. */
};

struct manifest_rule {
	char *if_expr; /**< Expression: "target != linux", etc. */
};

struct manifest_dep {
	char *name;	 /**< e.g. "espressif/cmp" */
	char *spec;	 /**< Version constraint; NULL if omitted */
	int is_public;	 /**< -1 unset, 0 private, 1 public */
	int pre_release; /**< 0 or 1 */
	enum manifest_dep_source source;
	char *git_url;	   /**< When @c source is GIT */
	char *path;	   /**< When @c source is PATH */
	char *service_url; /**< Registry override (registry source) */
	char *if_expr;	   /**< Top-level @c if: */
	struct manifest_rule *rules;
	size_t rules_nr;
};

struct manifest {
	char *version;	   /**< Own version; NULL if not declared */
	char *description; /**< NULL if not declared */
	struct manifest_dep *deps;
	size_t deps_nr;
	char **targets; /**< NULL if no filter */
	size_t targets_nr;
};

/** Static initializer for an empty manifest. */
#define MANIFEST_INIT {0}

/**
 * @brief Parse a manifest from a YAML buffer.
 *
 * On success, fills @p out with freshly allocated state that the
 * caller releases via @c manifest_release().  On failure, @p out is
 * left untouched and does not need to be released.
 *
 * @return 0 on success, -1 on parse error.
 */
int manifest_parse(struct manifest *out, const char *text, size_t len);

/** Convenience wrapper: load from a file. */
int manifest_load(struct manifest *out, const char *path);

/** Release everything @p m owns; leaves it in the init state. */
void manifest_release(struct manifest *m);

#endif /* CMD_IDF_COMPONENT_MANIFEST_H */
