#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

/**
 * @file schema.hpp
 * @brief Compile-time field descriptions, schema validation, and named-field lookup.
 */

/**
 * @brief Public types and compile-time utilities for the FieldPack library.
 */
namespace fieldpack {

/**
 * @brief Describe one named field without allocating or storing a value.
 *
 * A tag is a C++ type used as the field's name. FieldPack compares tags by
 * exact type identity, so unrelated types with the same spelling remain
 * distinct. The descriptor itself is intentionally unconstrained so that
 * invalid descriptions can be inspected with @ref fieldpack::valid_field.
 *
 * @tparam Tag Exact type used to identify the field.
 * @tparam T Value type associated with @p Tag.
 *
 * @code{.cpp}
 * struct position {};
 * using position_field = fieldpack::field<position, float>;
 *
 * static_assert(std::same_as<position_field::tag, position>);
 * static_assert(std::same_as<position_field::type, float>);
 * @endcode
 */
template<class Tag, class T> struct field {
    /** @brief Exact type used as this field's name. */
    using tag = Tag;

    /** @brief Value type stored for this field by a table backend. */
    using type = T;
};

/**
 * @brief Describe an ordered collection of fields.
 *
 * A schema is compile-time metadata and owns no storage. Its field order is
 * available to storage backends through an internal index, but named access
 * is based only on tag type. Reordering fields therefore changes positional
 * metadata without changing the result of @ref field_type_t for any tag.
 *
 * This primary descriptor remains formable for empty, malformed, or
 * duplicate field lists. Use @ref fieldpack::valid_schema to decide whether
 * consumers may use a description.
 *
 * @tparam Fields Candidate field descriptors in their declared order.
 *
 * @code{.cpp}
 * struct x {};
 * struct id {};
 *
 * using first = fieldpack::schema<
 *     fieldpack::field<x, float>,
 *     fieldpack::field<id, unsigned>>;
 * using reordered = fieldpack::schema<
 *     fieldpack::field<id, unsigned>,
 *     fieldpack::field<x, float>>;
 *
 * static_assert(fieldpack::valid_schema<first>);
 * static_assert(fieldpack::valid_schema<reordered>);
 * static_assert(std::same_as<fieldpack::field_type_t<first, x>, float>);
 * static_assert(std::same_as<fieldpack::field_type_t<reordered, x>, float>);
 * @endcode
 */
template<class... Fields> struct schema {};

/**
 * @brief Implementation machinery for schema recognition and lookup.
 *
 * These entities are documented for maintainers but are not part of
 * FieldPack's compatibility-stable public API.
 */
namespace detail {

/**
 * @brief Recognize non-field types without attempting to read nested aliases.
 *
 * The unspecialized trait deliberately supplies no `tag` or `type`. Validation
 * recognizes field specializations before any helper can require those
 * members, keeping diagnostics for malformed schema entries short.
 *
 * @tparam Candidate Type to inspect.
 */
template<class Candidate> struct field_traits {
    /** @brief Whether @p Candidate is an unqualified @ref field specialization. */
    static constexpr bool is_field = false;
};

/**
 * @brief Expose metadata from an unqualified @ref field specialization.
 *
 * @tparam Tag Exact field tag type.
 * @tparam T Candidate field value type.
 */
template<class Tag, class T> struct field_traits<field<Tag, T>> {
    /** @brief Marks this specialization as a recognized field descriptor. */
    static constexpr bool is_field = true;

    /** @brief Tag extracted from the field descriptor. */
    using tag = Tag;

    /** @brief Value type extracted from the field descriptor. */
    using type = T;
};

/**
 * @brief Check the first-milestone requirements for a field value type.
 *
 * The arithmetic test is evaluated first with `if constexpr`. Consequently,
 * later traits that can require a complete type are not instantiated for an
 * unrelated non-arithmetic candidate.
 *
 * @tparam T Candidate value type.
 * @return `true` when @p T is non-cv arithmetic, is not `bool`, and is
 * trivially copyable; otherwise `false`.
 */
template<class T> consteval auto valid_field_value_type() noexcept -> bool
{
    if constexpr (!std::is_arithmetic_v<T>) {
        return false;
    }
    else {
        return !std::same_as<T, bool> && std::same_as<T, std::remove_cv_t<T>> && std::is_trivially_copyable_v<T>;
    }
}

/**
 * @brief Report `false` for types that are not @ref field specializations.
 *
 * @tparam Candidate Unqualified descriptor candidate.
 */
template<class Candidate> struct valid_field_impl : std::false_type {};

/**
 * @brief Validate the value type of a recognized @ref field descriptor.
 *
 * @tparam Tag Exact field tag type.
 * @tparam T Candidate field value type.
 */
template<class Tag, class T>
struct valid_field_impl<field<Tag, T>> : std::bool_constant<valid_field_value_type<T>()> {};

/**
 * @brief Base case for exact type-uniqueness detection.
 *
 * Empty and single-type packs are unique.
 *
 * @tparam Types Types remaining in the comparison.
 */
template<class... Types> struct unique_types : std::true_type {};

/**
 * @brief Recursively verify that @p First differs from every remaining type.
 *
 * @tparam First Type compared with the rest of the pack.
 * @tparam Rest Remaining types, which are also checked recursively.
 */
template<class First, class... Rest>
struct unique_types<First, Rest...>
    : std::bool_constant<(!std::same_as<First, Rest> && ...) && unique_types<Rest...>::value> {};

} // namespace detail

/**
 * @brief Determine whether a type is a supported field descriptor.
 *
 * Top-level cv-qualification on the descriptor is ignored. Cv-qualification
 * on the field's value type is not ignored and makes the field invalid. Tags
 * themselves may be any type and retain their exact cv-qualification.
 *
 * @tparam Candidate Type to validate as a field descriptor.
 *
 * @code{.cpp}
 * struct x {};
 * using value = fieldpack::field<x, float>;
 *
 * static_assert(fieldpack::valid_field<value>);
 * static_assert(fieldpack::valid_field<const value>);
 * static_assert(!fieldpack::valid_field<fieldpack::field<x, const float>>);
 * static_assert(!fieldpack::valid_field<fieldpack::field<x, bool>>);
 * @endcode
 */
template<class Candidate>
concept valid_field = detail::valid_field_impl<std::remove_cv_t<Candidate>>::value;

namespace detail {

/**
 * @brief Validate the entries and tag uniqueness of a schema field pack.
 *
 * Checks are ordered with `if constexpr`: emptiness is rejected first, then
 * malformed or unsupported fields, then top-level cv-qualified descriptors.
 * Tag aliases are inspected only after all earlier checks succeed.
 *
 * @tparam Fields Candidate entries from a @ref schema specialization.
 * @return `true` if the pack is non-empty, contains only unqualified valid
 * fields, and has unique exact tag types.
 */
template<class... Fields> consteval auto valid_schema_fields() noexcept -> bool
{
    if constexpr (sizeof...(Fields) == 0) {
        return false;
    }
    else if constexpr (!(valid_field<Fields> && ...)) {
        return false;
    }
    else if constexpr (!(std::same_as<Fields, std::remove_cv_t<Fields>> && ...)) {
        return false;
    }
    else {
        return unique_types<typename field_traits<Fields>::tag...>::value;
    }
}

/**
 * @brief Report `false` for types that are not @ref schema specializations.
 *
 * @tparam Candidate Unqualified schema candidate.
 */
template<class Candidate> struct valid_schema_impl : std::false_type {};

/**
 * @brief Delegate validation of a recognized schema to its field pack.
 *
 * @tparam Fields Entries declared by the schema.
 */
template<class... Fields>
struct valid_schema_impl<schema<Fields...>> : std::bool_constant<valid_schema_fields<Fields...>()> {};

/**
 * @brief Primary declaration for metadata available only on schemas.
 *
 * The primary template is intentionally incomplete. Public consumers are
 * constrained by @ref fieldpack::valid_schema before instantiating its
 * specialization.
 *
 * @tparam Schema Unqualified schema type.
 */
template<class Schema> struct schema_traits;

/**
 * @brief Store metadata that depends only on a schema's field pack.
 *
 * @tparam Fields Entries declared by the schema.
 */
template<class... Fields> struct schema_traits<schema<Fields...>> {
    /** @brief Number of declared fields. */
    static constexpr std::size_t field_count = sizeof...(Fields);
};

/**
 * @brief Primary declaration for exact tag-presence lookup.
 *
 * @tparam Schema Unqualified schema type.
 * @tparam Tag Exact tag type to find.
 */
template<class Schema, class Tag> struct contains_tag_impl;

/**
 * @brief Fold over a schema's fields to find an exact tag type.
 *
 * @tparam Fields Entries declared by the schema.
 * @tparam Tag Exact tag type to find.
 */
template<class... Fields, class Tag>
struct contains_tag_impl<schema<Fields...>, Tag>
    : std::bool_constant<(std::same_as<Tag, typename field_traits<Fields>::tag> || ...)> {};

} // namespace detail

/**
 * @brief Determine whether a type is a usable first-milestone schema.
 *
 * A valid schema is non-empty, contains only unqualified valid field
 * descriptors, and has no duplicate exact tag types. Top-level
 * cv-qualification on the schema itself is ignored.
 *
 * @tparam Candidate Type to validate as a schema.
 *
 * @code{.cpp}
 * struct x {};
 * using valid = fieldpack::schema<fieldpack::field<x, float>>;
 * using empty = fieldpack::schema<>;
 * using duplicate = fieldpack::schema<
 *     fieldpack::field<x, float>,
 *     fieldpack::field<x, double>>;
 *
 * static_assert(fieldpack::valid_schema<valid>);
 * static_assert(fieldpack::valid_schema<const valid>);
 * static_assert(!fieldpack::valid_schema<empty>);
 * static_assert(!fieldpack::valid_schema<duplicate>);
 * @endcode
 */
template<class Candidate>
concept valid_schema = detail::valid_schema_impl<std::remove_cv_t<Candidate>>::value;

/**
 * @brief Number of fields declared by a valid schema.
 *
 * Top-level cv-qualification on @p Schema is ignored. The variable template
 * is unavailable for invalid schemas because its constraint is not satisfied.
 *
 * @tparam Schema Valid schema type to inspect.
 *
 * @code{.cpp}
 * struct x {};
 * struct y {};
 * using coordinates = fieldpack::schema<
 *     fieldpack::field<x, float>,
 *     fieldpack::field<y, float>>;
 *
 * static_assert(fieldpack::field_count_v<coordinates> == 2);
 * static_assert(fieldpack::field_count_v<const coordinates> == 2);
 * @endcode
 */
template<class Schema>
    requires valid_schema<Schema>
inline constexpr std::size_t field_count_v = detail::schema_traits<std::remove_cv_t<Schema>>::field_count;

/**
 * @brief Determine whether a valid schema contains an exact tag type.
 *
 * @tparam Schema Valid schema type to search.
 * @tparam Tag Exact tag type to find.
 *
 * @code{.cpp}
 * struct x {};
 * struct missing {};
 * using values = fieldpack::schema<fieldpack::field<x, float>>;
 *
 * static_assert(fieldpack::contains_tag_v<values, x>);
 * static_assert(!fieldpack::contains_tag_v<values, const x>);
 * static_assert(!fieldpack::contains_tag_v<values, missing>);
 * @endcode
 */
template<class Schema, class Tag>
    requires valid_schema<Schema>
inline constexpr bool contains_tag_v = detail::contains_tag_impl<std::remove_cv_t<Schema>, Tag>::value;

namespace detail {

/**
 * @brief Recursively compute a tag's zero-based position in a field pack.
 *
 * There is deliberately no empty-pack definition. The public-facing variable
 * template @ref field_index_v requires the tag to be present before recursion
 * begins, so reaching an empty pack would indicate an internal contract bug.
 *
 * @tparam Tag Exact tag type to locate.
 * @tparam First First field still under consideration.
 * @tparam Rest Remaining fields.
 */
template<class Tag, class First, class... Rest> struct field_index_impl;

/**
 * @brief Dispatch field-index recursion according to whether the first field matches.
 *
 * Separating match dispatch from recursion avoids instantiating the recursive
 * branch after a tag has been found.
 *
 * @tparam Tag Exact tag type to locate.
 * @tparam Matches Whether @p First has tag type @p Tag.
 * @tparam First First field still under consideration.
 * @tparam Rest Remaining fields.
 */
template<class Tag, bool Matches, class First, class... Rest> struct field_index_step;

/**
 * @brief Terminate field-index recursion at the matching field.
 *
 * @tparam Tag Exact tag type being located.
 * @tparam First Matching field descriptor.
 * @tparam Rest Fields after the match, which need not be inspected.
 */
template<class Tag, class First, class... Rest>
struct field_index_step<Tag, true, First, Rest...> : std::integral_constant<std::size_t, 0> {};

/**
 * @brief Skip a non-matching field and add one to the recursive result.
 *
 * @tparam Tag Exact tag type being located.
 * @tparam First Non-matching field descriptor.
 * @tparam Rest Remaining fields, one of which is guaranteed to match.
 */
template<class Tag, class First, class... Rest>
struct field_index_step<Tag, false, First, Rest...>
    : std::integral_constant<std::size_t, 1 + field_index_impl<Tag, Rest...>::value> {};

/**
 * @brief Select the matching or recursive field-index step.
 *
 * @tparam Tag Exact tag type being located.
 * @tparam First First field still under consideration.
 * @tparam Rest Remaining fields.
 */
template<class Tag, class First, class... Rest>
struct field_index_impl : field_index_step<Tag, std::same_as<Tag, typename field_traits<First>::tag>, First, Rest...> {
};

/**
 * @brief Primary declaration adapting field-pack index lookup to a schema.
 *
 * @tparam Schema Unqualified schema type.
 * @tparam Tag Exact tag type to locate.
 */
template<class Schema, class Tag> struct schema_field_index_impl;

/**
 * @brief Unpack a schema and start recursive field-index lookup.
 *
 * @tparam Fields Entries declared by the schema.
 * @tparam Tag Exact tag type to locate.
 */
template<class... Fields, class Tag>
struct schema_field_index_impl<schema<Fields...>, Tag> : field_index_impl<Tag, Fields...> {};

/**
 * @brief Zero-based declaration index of a tag in a valid schema.
 *
 * This positional result is an implementation detail used to select metadata
 * and storage. User-facing access remains tag-based. Both the schema-validity
 * and tag-presence constraints prevent uncontrolled recursive diagnostics.
 *
 * @tparam Schema Valid schema type to inspect.
 * @tparam Tag Exact tag type known to occur in @p Schema.
 */
template<class Schema, class Tag>
    requires(valid_schema<Schema> && contains_tag_v<Schema, Tag>)
inline constexpr std::size_t field_index_v = schema_field_index_impl<std::remove_cv_t<Schema>, Tag>::value;

/**
 * @brief Recursively select a field descriptor by zero-based position.
 *
 * There is intentionally no out-of-range base case. Callers obtain @p Index
 * from constrained tag lookup, which guarantees a valid position.
 *
 * @tparam Index Number of fields to skip.
 * @tparam First First field still under consideration.
 * @tparam Rest Remaining fields.
 */
template<std::size_t Index, class First, class... Rest> struct field_at_impl {
    /** @brief Field descriptor selected after skipping @p Index entries. */
    using type = typename field_at_impl<Index - 1, Rest...>::type;
};

/**
 * @brief Return the field at the current position.
 *
 * @tparam First Selected field descriptor.
 * @tparam Rest Fields after the selected field.
 */
template<class First, class... Rest> struct field_at_impl<0, First, Rest...> {
    /** @brief Selected field descriptor. */
    using type = First;
};

/**
 * @brief Primary declaration adapting positional selection to a schema.
 *
 * @tparam Schema Unqualified schema type.
 * @tparam Index Valid zero-based field position.
 */
template<class Schema, std::size_t Index> struct schema_field_at_impl;

/**
 * @brief Unpack a schema and select the descriptor at @p Index.
 *
 * @tparam Fields Entries declared by the schema.
 * @tparam Index Valid zero-based field position.
 */
template<class... Fields, std::size_t Index>
struct schema_field_at_impl<schema<Fields...>, Index> : field_at_impl<Index, Fields...> {};

/**
 * @brief Resolve a tag to its field descriptor and then to its value type.
 *
 * @tparam Schema Unqualified valid schema type.
 * @tparam Tag Exact tag type known to occur in @p Schema.
 */
template<class Schema, class Tag> struct field_type_impl {
    /** @brief Field descriptor selected by the tag's declaration index. */
    using selected_field = typename schema_field_at_impl<Schema, field_index_v<Schema, Tag>>::type;

    /** @brief Value type extracted from @ref selected_field. */
    using type = typename field_traits<selected_field>::type;
};

} // namespace detail

/**
 * @brief Value type associated with an exact tag in a valid schema.
 *
 * The alias is available only when @p Schema satisfies
 * @ref fieldpack::valid_schema and contains @p Tag. An unknown tag therefore
 * fails a `requires` expression cleanly instead of instantiating recursive
 * lookup machinery.
 *
 * @tparam Schema Valid schema type to inspect.
 * @tparam Tag Exact tag type whose value type is requested.
 *
 * @code{.cpp}
 * struct x {};
 * struct missing {};
 * using values = fieldpack::schema<fieldpack::field<x, float>>;
 *
 * static_assert(std::same_as<fieldpack::field_type_t<values, x>, float>);
 *
 * template<class Schema, class Tag>
 * concept has_field_type = requires {
 *     typename fieldpack::field_type_t<Schema, Tag>;
 * };
 * static_assert(!has_field_type<values, missing>);
 * @endcode
 */
template<class Schema, class Tag>
    requires(valid_schema<Schema> && contains_tag_v<Schema, Tag>)
using field_type_t = typename detail::field_type_impl<std::remove_cv_t<Schema>, Tag>::type;

} // namespace fieldpack
