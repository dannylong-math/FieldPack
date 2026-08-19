#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide header convention

/**
 * @file fieldpack.hpp
 * @brief Convenience header for the complete public FieldPack API.
 *
 * Including this header makes schema definition, layout tags, tables, row
 * access, access descriptors, and chunk traversal available. Applications
 * that prefer narrower dependencies may include the individual public headers
 * instead. Headers below `fieldpack/detail` are intentionally not part of this
 * umbrella or the compatibility-stable public API.
 *
 * @code{.cpp}
 * #include <fieldpack/fieldpack.hpp>
 * @endcode
 */

#include <fieldpack/execution.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <fieldpack/table.hpp>
