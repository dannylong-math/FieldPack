#pragma once

#include <concepts>
#include <type_traits>

/**
 * @file layout.hpp
 * @brief Public storage-layout tags and compile-time layout metadata.
 */

namespace fieldpack {

/**
 * @brief Select structure-of-arrays storage.
 *
 * A table using this layout owns one contiguous allocation per schema field.
 * Unlike an AoSoA layout, SoA has no physical tile extent.
 *
 * @code{.cpp}
 * using particles = fieldpack::table<particle_schema, fieldpack::soa>;
 * @endcode
 */
struct soa {};

/**
 * @brief Implementation machinery for recognizing layout tags.
 *
 * These traits are documented for maintainers but are not independently part
 * of FieldPack's compatibility-stable public API.
 */
namespace detail {

/**
 * @brief Primary declaration for metadata associated with a layout tag.
 *
 * The incomplete primary template deliberately provides no metadata. Public
 * queries therefore reject unknown layout types through constraints instead
 * of assigning them accidental default behavior.
 *
 * @tparam Layout Unqualified candidate layout tag.
 */
template<class Layout> struct layout_traits_impl;

/**
 * @brief Metadata for structure-of-arrays storage.
 */
template<> struct layout_traits_impl<soa> {
    /** @brief SoA columns are not divided into physical tiles. */
    static constexpr bool is_tiled = false;
};

} // namespace detail

/**
 * @brief Expose compile-time metadata for a recognized layout.
 *
 * @tparam Layout Layout tag to inspect.
 *
 * @code{.cpp}
 * static_assert(!fieldpack::layout_traits<fieldpack::soa>::is_tiled);
 * @endcode
 */
template<class Layout> struct layout_traits : detail::layout_traits_impl<Layout> {};

/**
 * @brief Determine whether a type is a recognized storage-layout tag.
 *
 * @tparam Layout Candidate layout type.
 */
template<class Layout>
concept valid_layout = std::same_as<Layout, std::remove_cv_t<Layout>> && requires {
    { detail::layout_traits_impl<Layout>::is_tiled } -> std::convertible_to<bool>;
};

} // namespace fieldpack
