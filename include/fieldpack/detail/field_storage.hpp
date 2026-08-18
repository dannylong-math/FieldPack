#pragma once

#include <fieldpack/detail/aligned_allocator.hpp>
#include <vector>

/**
 * @file field_storage.hpp
 * @brief Internal contiguous storage for one arithmetic field.
 */

namespace fieldpack::detail {

/**
 * @brief Own a contiguous, aligned sequence of values for one field.
 *
 * This alias deliberately delegates element lifetime, value initialization,
 * copy and move behavior, and exception safety to `std::vector`. Its allocator
 * guarantees that every non-empty allocation begins at @ref default_alignment.
 * Constructing the storage with a count value-initializes exactly that many
 * elements.
 *
 * The allocation policy parameter is an internal test seam. Layout backends
 * use the default aligned-new policy, while focused tests may inject a
 * deterministic failure policy.
 *
 * @tparam T Unqualified object type stored contiguously.
 * @tparam AllocationPolicy Stateless raw byte-allocation policy used by the
 * underlying allocator.
 */
template<class T, class AllocationPolicy = aligned_new_policy>
using field_storage = std::vector<T, aligned_allocator<T, default_alignment, AllocationPolicy>>;

} // namespace fieldpack::detail
