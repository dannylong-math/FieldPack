#include <fieldpack/execution.hpp>
#include <type_traits>

namespace {
struct value {};
using access = fieldpack::field_access<fieldpack::read<value>>;
static_assert(std::is_empty_v<access>);
} // namespace

int main() {}
