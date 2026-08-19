#include <fieldpack/execution.hpp>

namespace {
struct value {};
using access = fieldpack::field_access<fieldpack::read<value>>;
static_assert(sizeof(access) > 0U);
} // namespace

int main() {}
