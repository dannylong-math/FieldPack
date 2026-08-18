#include <concepts>
#include <cstddef>
#include <cstdint>
#include <fieldpack/schema.hpp>
#include <type_traits>

namespace {

struct x {};
struct y {};
struct id {};
struct count {};

namespace first_namespace {
struct position {};
} // namespace first_namespace

namespace second_namespace {
struct position {};
} // namespace second_namespace

struct trivial_non_arithmetic {
    int value;
};

struct non_trivially_copyable {
    ~non_trivially_copyable() {}
};

template<class Schema, class Tag>
concept has_field_type = requires { typename fieldpack::field_type_t<Schema, Tag>; };

template<class Schema, class Tag>
concept has_field_index =
    requires { std::integral_constant<std::size_t, fieldpack::detail::field_index_v<Schema, Tag>>{}; };

using x_field = fieldpack::field<x, float>;
using y_field = fieldpack::field<y, double>;
using id_field = fieldpack::field<id, std::uint32_t>;
using count_field = fieldpack::field<count, std::int64_t>;

static_assert(fieldpack::valid_field<x_field>);
static_assert(fieldpack::valid_field<y_field>);
static_assert(fieldpack::valid_field<id_field>);
static_assert(fieldpack::valid_field<count_field>);

using mixed_schema = fieldpack::schema<id_field, x_field, count_field, y_field>;
using reordered_schema = fieldpack::schema<y_field, count_field, x_field, id_field>;

static_assert(fieldpack::valid_schema<mixed_schema>);
static_assert(fieldpack::valid_schema<reordered_schema>);

static_assert(fieldpack::field_count_v<mixed_schema> == 4);
static_assert(fieldpack::field_count_v<reordered_schema> == 4);

static_assert(fieldpack::contains_tag_v<mixed_schema, x>);
static_assert(fieldpack::contains_tag_v<mixed_schema, y>);
static_assert(fieldpack::contains_tag_v<mixed_schema, id>);
static_assert(fieldpack::contains_tag_v<mixed_schema, count>);

static_assert(std::same_as<fieldpack::field_type_t<mixed_schema, x>, float>);
static_assert(std::same_as<fieldpack::field_type_t<mixed_schema, y>, double>);
static_assert(std::same_as<fieldpack::field_type_t<mixed_schema, id>, std::uint32_t>);
static_assert(std::same_as<fieldpack::field_type_t<mixed_schema, count>, std::int64_t>);

static_assert(std::same_as<fieldpack::field_type_t<reordered_schema, x>, float>);
static_assert(std::same_as<fieldpack::field_type_t<reordered_schema, y>, double>);
static_assert(std::same_as<fieldpack::field_type_t<reordered_schema, id>, std::uint32_t>);
static_assert(std::same_as<fieldpack::field_type_t<reordered_schema, count>, std::int64_t>);

static_assert(fieldpack::detail::field_index_v<mixed_schema, id> == 0);
static_assert(fieldpack::detail::field_index_v<mixed_schema, x> == 1);
static_assert(fieldpack::detail::field_index_v<mixed_schema, count> == 2);
static_assert(fieldpack::detail::field_index_v<mixed_schema, y> == 3);

static_assert(fieldpack::detail::field_index_v<reordered_schema, y> == 0);
static_assert(fieldpack::detail::field_index_v<reordered_schema, count> == 1);
static_assert(fieldpack::detail::field_index_v<reordered_schema, x> == 2);
static_assert(fieldpack::detail::field_index_v<reordered_schema, id> == 3);

// Schema metadata queries ignore top-level cv-qualification on the schema.
static_assert(fieldpack::valid_schema<const mixed_schema>);
static_assert(fieldpack::valid_schema<volatile mixed_schema>);
static_assert(fieldpack::valid_schema<const volatile mixed_schema>);
static_assert(fieldpack::field_count_v<const mixed_schema> == fieldpack::field_count_v<mixed_schema>);
static_assert(fieldpack::contains_tag_v<const volatile mixed_schema, y>);
static_assert(std::same_as<fieldpack::field_type_t<const mixed_schema, y>, double>);
static_assert(fieldpack::detail::field_index_v<volatile mixed_schema, y> == 3);

// Tags use exact C++ type identity. Equal spelling in different namespaces,
// and cv-qualified tag types, do not alias one another.
using distinct_tag_schema =
    fieldpack::schema<fieldpack::field<first_namespace::position, float>,
                      fieldpack::field<second_namespace::position, double>, fieldpack::field<x, std::int32_t>,
                      fieldpack::field<const x, std::uint32_t>>;

static_assert(fieldpack::valid_schema<distinct_tag_schema>);
static_assert(fieldpack::contains_tag_v<distinct_tag_schema, first_namespace::position>);
static_assert(fieldpack::contains_tag_v<distinct_tag_schema, second_namespace::position>);
static_assert(fieldpack::contains_tag_v<distinct_tag_schema, x>);
static_assert(fieldpack::contains_tag_v<distinct_tag_schema, const x>);
static_assert(std::same_as<fieldpack::field_type_t<distinct_tag_schema, first_namespace::position>, float>);
static_assert(std::same_as<fieldpack::field_type_t<distinct_tag_schema, second_namespace::position>, double>);
static_assert(std::same_as<fieldpack::field_type_t<distinct_tag_schema, x>, std::int32_t>);
static_assert(std::same_as<fieldpack::field_type_t<distinct_tag_schema, const x>, std::uint32_t>);

// Invalid field and schema descriptions remain formable so their validity can
// be queried without triggering diagnostics from their primary templates.
using bool_field = fieldpack::field<x, bool>;
using const_field = fieldpack::field<x, const float>;
using volatile_field = fieldpack::field<x, volatile float>;
using trivial_class_field = fieldpack::field<x, trivial_non_arithmetic>;
using non_trivial_field = fieldpack::field<x, non_trivially_copyable>;

static_assert(std::is_trivially_copyable_v<trivial_non_arithmetic>);
static_assert(!std::is_arithmetic_v<trivial_non_arithmetic>);
static_assert(!std::is_trivially_copyable_v<non_trivially_copyable>);

static_assert(!fieldpack::valid_field<bool_field>);
static_assert(!fieldpack::valid_field<const_field>);
static_assert(!fieldpack::valid_field<volatile_field>);
static_assert(!fieldpack::valid_field<trivial_class_field>);
static_assert(!fieldpack::valid_field<non_trivial_field>);
static_assert(!fieldpack::valid_field<int>);

using empty_schema = fieldpack::schema<>;
using duplicate_schema = fieldpack::schema<x_field, fieldpack::field<x, double>>;
using bool_schema = fieldpack::schema<bool_field>;
using const_value_schema = fieldpack::schema<const_field>;
using volatile_value_schema = fieldpack::schema<volatile_field>;
using trivial_class_schema = fieldpack::schema<trivial_class_field>;
using non_trivial_schema = fieldpack::schema<non_trivial_field>;
using malformed_schema = fieldpack::schema<int>;

static_assert(!fieldpack::valid_schema<empty_schema>);
static_assert(!fieldpack::valid_schema<duplicate_schema>);
static_assert(!fieldpack::valid_schema<bool_schema>);
static_assert(!fieldpack::valid_schema<const_value_schema>);
static_assert(!fieldpack::valid_schema<volatile_value_schema>);
static_assert(!fieldpack::valid_schema<trivial_class_schema>);
static_assert(!fieldpack::valid_schema<non_trivial_schema>);
static_assert(!fieldpack::valid_schema<malformed_schema>);

struct unknown {};

static_assert(!fieldpack::contains_tag_v<mixed_schema, unknown>);
static_assert(!has_field_type<mixed_schema, unknown>);
static_assert(!has_field_index<mixed_schema, unknown>);

} // namespace

int main() {}
