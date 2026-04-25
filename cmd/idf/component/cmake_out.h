/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cmd/idf/component/cmake_out.h
 * @brief Emitters for the CMake-facing temp files.
 *
 * Produces the two build-time artefacts that @c ice idf component
 * prepare has to hand back to IDF's CMake integration:
 *
 *   -# @c managed_components_list.temp.cmake -- an include file CMake
 *      reads to discover managed-component paths.  Contains
 *      @c idf_component_set_property + @c idf_build_component
 *      directives and a trailing @c set(managed_components "a;b;c").
 *   -# @c components_with_manifests_list.temp -- a newline-separated
 *      list of absolute component paths, consumed by the later
 *      @c inject step.
 *
 * Format matches the Python @c idf_component_manager output closely
 * enough for diff-based byte comparison against real goldens once we
 * capture them.  Output is deterministic: entries are sorted by name
 * / path internally before emission.
 */
#ifndef CMD_IDF_COMPONENT_CMAKE_OUT_H
#define CMD_IDF_COMPONENT_CMAKE_OUT_H

#include <stddef.h>

/** A downloaded component the solver resolved to a registry/git source. */
struct cmake_dl_component {
	const char *name;	    /**< e.g. "espressif/cjson" */
	const char *abs_path;	    /**< POSIX-style absolute path */
	const char *version;	    /**< Resolved version string */
	const char *const *targets; /**< Chip names or NULL */
	size_t targets_nr;
};

/** A local component (in-project) that carries its own manifest. */
struct cmake_local_component {
	const char *name;    /**< e.g. "main" */
	const char *version; /**< NULL if manifest has none */
};

/**
 * @brief Write the managed_components_list include file.
 *
 * Output layout:
 *
 *   idf_component_set_property(<LOCAL> COMPONENT_VERSION "...")  # if version
 *   ...
 *   idf_build_component("<ABS_PATH>" "project_managed_components")
 *   idf_component_set_property(<DL> COMPONENT_VERSION "...")
 *   idf_component_set_property(<DL> REQUIRED_IDF_TARGETS "t1 t2")
 *   ...
 *   set(managed_components "a;b;c")
 *
 * Locals are emitted in their input order; downloads are sorted by
 * name (matching the Python tool).
 *
 * @return 0 on success, -1 on I/O failure.
 */
int cmake_out_emit_managed_list(const char *path,
				const struct cmake_local_component *locals,
				size_t nr_locals,
				const struct cmake_dl_component *downloads,
				size_t nr_downloads);

/**
 * @brief Write the components-with-manifests sidecar file.
 *
 * One absolute path per line, sorted ascending, no trailing newline
 * (matching the Python @c '\n'.join(...) shape).
 *
 * @return 0 on success, -1 on I/O failure.
 */
int cmake_out_emit_components_paths(const char *path, const char *const *paths,
				    size_t nr);

#endif /* CMD_IDF_COMPONENT_CMAKE_OUT_H */
