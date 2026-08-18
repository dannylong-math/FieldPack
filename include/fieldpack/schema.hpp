#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace fieldpack {

template<class Tag, class T> struct field {
    using tag = Tag;
    using type = T;
};

template<class... Fields> struct schema {};

namespace detail {

template<class Candidate> struct field_traits {
    static constexpr bool is_field = false;
};

template<class Tag, class T> struct field_traits<field<Tag, T>> {
    static constexpr bool is_field = true;
    using tag = Tag;
    using type = T;
};

template<class T> consteval auto valid_field_value_type() noexcept -> bool
{
    if constexpr (!std::is_arithmetic_v<T>) {
        return false;
    }
    else {
        return !std::same_as<T, bool> && std::same_as<T, std::remove_cv_t<T>> && std::is_trivially_copyable_v<T>;
    }
}

template<class Candidate> struct valid_field_impl : std::false_type {};

template<class Tag, class T>
struct valid_field_impl<field<Tag, T>> : std::bool_constant<valid_field_value_type<T>()> {};

template<class... Types> struct unique_types : std::true_type {};

template<class First, class... Rest>
struct unique_types<First, Rest...>
    : std::bool_constant<(!std::same_as<First, Rest> && ...) && unique_types<Rest...>::value> {};

} // namespace detail

template<class Candidate>
concept valid_field = detail::valid_field_impl<std::remove_cv_t<Candidate>>::value;

namespace detail {

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

template<class Candidate> struct valid_schema_impl : std::false_type {};

template<class... Fields>
struct valid_schema_impl<schema<Fields...>> : std::bool_constant<valid_schema_fields<Fields...>()> {};

template<class Schema> struct schema_traits;

template<class... Fields> struct schema_traits<schema<Fields...>> {
    static constexpr std::size_t field_count = sizeof...(Fields);
};

template<class Schema, class Tag> struct contains_tag_impl;

template<class... Fields, class Tag>
struct contains_tag_impl<schema<Fields...>, Tag>
    : std::bool_constant<(std::same_as<Tag, typename field_traits<Fields>::tag> || ...)> {};

} // namespace detail

template<class Candidate>
concept valid_schema = detail::valid_schema_impl<std::remove_cv_t<Candidate>>::value;

template<class Schema>
    requires valid_schema<Schema>
inline constexpr std::size_t field_count_v = detail::schema_traits<std::remove_cv_t<Schema>>::field_count;

template<class Schema, class Tag>
    requires valid_schema<Schema>
inline constexpr bool contains_tag_v = detail::contains_tag_impl<std::remove_cv_t<Schema>, Tag>::value;

namespace detail {

template<class Tag, class First, class... Rest> struct field_index_impl;

template<class Tag, bool Matches, class First, class... Rest> struct field_index_step;

template<class Tag, class First, class... Rest>
struct field_index_step<Tag, true, First, Rest...> : std::integral_constant<std::size_t, 0> {};

template<class Tag, class First, class... Rest>
struct field_index_step<Tag, false, First, Rest...>
    : std::integral_constant<std::size_t, 1 + field_index_impl<Tag, Rest...>::value> {};

template<class Tag, class First, class... Rest>
struct field_index_impl : field_index_step<Tag, std::same_as<Tag, typename field_traits<First>::tag>, First, Rest...> {
};

template<class Schema, class Tag> struct schema_field_index_impl;

template<class... Fields, class Tag>
struct schema_field_index_impl<schema<Fields...>, Tag> : field_index_impl<Tag, Fields...> {};

template<class Schema, class Tag>
    requires(valid_schema<Schema> && contains_tag_v<Schema, Tag>)
inline constexpr std::size_t field_index_v = schema_field_index_impl<std::remove_cv_t<Schema>, Tag>::value;

template<std::size_t Index, class First, class... Rest> struct field_at_impl : field_at_impl<Index - 1, Rest...> {};

template<class First, class... Rest> struct field_at_impl<0, First, Rest...> {
    using type = First;
};

template<class Schema, std::size_t Index> struct schema_field_at_impl;

template<class... Fields, std::size_t Index>
struct schema_field_at_impl<schema<Fields...>, Index> : field_at_impl<Index, Fields...> {};

template<class Schema, class Tag> struct field_type_impl {
    using selected_field = typename schema_field_at_impl<Schema, field_index_v<Schema, Tag>>::type;
    using type = typename field_traits<selected_field>::type;
};

} // namespace detail

template<class Schema, class Tag>
    requires(valid_schema<Schema> && contains_tag_v<Schema, Tag>)
using field_type_t = typename detail::field_type_impl<std::remove_cv_t<Schema>, Tag>::type;

} // namespace fieldpack
