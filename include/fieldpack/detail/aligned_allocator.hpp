#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

/**
 * @file aligned_allocator.hpp
 * @brief Internal aligned allocation policy and standard-library allocator.
 */

namespace fieldpack::detail {

/**
 * @brief Alignment used by every non-empty host field allocation.
 *
 * Keeping this value layout-independent ensures that SoA columns and AoSoA
 * tile fields begin with the same alignment. It is an implementation detail,
 * not a promise that traversal code may unconditionally use aligned SIMD
 * loads.
 */
inline constexpr std::size_t default_alignment = 64;

/**
 * @brief Allocate and release raw bytes with aligned C++ allocation functions.
 *
 * This stateless default policy isolates the raw allocation mechanism from
 * allocator bookkeeping. Tests may replace it with another stateless policy
 * to inject failures and observe ownership without relying on memory pressure.
 */
struct aligned_new_policy {
    /**
     * @brief Allocate a non-empty block with the requested alignment.
     *
     * @param bytes Number of bytes to allocate; must be greater than zero.
     * @param alignment Valid power-of-two alignment for the allocation.
     * @return Pointer to the raw aligned block.
     * @throws std::bad_alloc When allocation cannot be satisfied.
     */
    [[nodiscard]] static auto allocate_bytes(std::size_t bytes, std::size_t alignment) -> void*
    {
        return ::operator new(bytes, std::align_val_t{alignment});
    }

    /**
     * @brief Release a block returned by @ref allocate_bytes.
     *
     * @param allocation Pointer returned by @ref allocate_bytes.
     * The byte-count argument preserves the common allocation-policy
     * interface but is not needed by the portable unsized aligned-delete
     * overload.
     *
     * @param alignment Original alignment supplied to @ref allocate_bytes.
     */
    static void deallocate_bytes(void* allocation, std::size_t /*unused*/, std::size_t alignment) noexcept
    {
        ::operator delete(allocation, std::align_val_t{alignment});
    }
};

/**
 * @brief Determine whether a type provides the raw allocation policy contract.
 *
 * Policies are deliberately stateless so all rebound allocator instances are
 * interchangeable. The static interface also keeps the production allocator
 * empty while allowing tests to keep counters in static policy data.
 *
 * @tparam Policy Candidate raw allocation policy.
 */
template<class Policy>
concept stateless_allocation_policy = std::is_empty_v<Policy> && std::is_default_constructible_v<Policy> &&
                                      requires(std::size_t bytes, std::size_t alignment, void* allocation) {
                                          { Policy::allocate_bytes(bytes, alignment) } -> std::same_as<void*>;
                                          {
                                              Policy::deallocate_bytes(allocation, bytes, alignment)
                                          } noexcept -> std::same_as<void>;
                                      };

/**
 * @brief Validate an allocator value type and its requested alignment.
 *
 * A usable value type is an unqualified object type. The alignment must be a
 * nonzero power of two and cannot be weaker than the type's natural alignment.
 *
 * @tparam T Candidate allocator value type.
 * @tparam Alignment Requested byte alignment.
 */
template<class T, std::size_t Alignment>
concept valid_allocator_alignment = std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> &&
                                    std::has_single_bit(Alignment) && Alignment >= alignof(T);

/**
 * @brief Standard-library allocator that starts every non-empty block at a
 * fixed byte alignment.
 *
 * The allocator owns no state. Allocators rebound to another value type but
 * using the same alignment and allocation policy are therefore always equal.
 * A count of zero has the stronger FieldPack-specific guarantee of returning
 * `nullptr` without invoking the raw allocation policy.
 *
 * @tparam T Unqualified object type stored in the allocation.
 * @tparam Alignment Required power-of-two alignment, defaulting to
 * @ref default_alignment.
 * @tparam AllocationPolicy Stateless raw byte-allocation policy.
 */
template<class T, std::size_t Alignment = default_alignment, class AllocationPolicy = aligned_new_policy>
    requires valid_allocator_alignment<T, Alignment> && stateless_allocation_policy<AllocationPolicy>
class aligned_allocator {
public:
    /** @brief Element type managed by this allocator. */
    using value_type = T;

    /** @brief Unsigned type used for element and byte counts. */
    using size_type = std::size_t;

    /** @brief Signed type used for pointer differences. */
    using difference_type = std::ptrdiff_t;

    /** @brief Advertise that every instance of this stateless allocator is interchangeable. */
    using is_always_equal = std::true_type;

    /**
     * @brief Rebind this allocator while retaining its alignment and policy.
     *
     * The explicit member is required because the allocator template contains
     * a non-type alignment parameter that generic allocator rebinding cannot
     * infer portably.
     *
     * @tparam U Replacement element type.
     */
    template<class U> struct rebind {
        /** @brief Allocator for @p U with the same alignment and policy. */
        using other = aligned_allocator<U, Alignment, AllocationPolicy>;
    };

    /** @brief Construct the stateless allocator. */
    constexpr aligned_allocator() noexcept = default;

    /**
     * @brief Construct from a rebound allocator with the same policy.
     *
     * @tparam U Source allocator's value type.
     */
    template<class U>
    constexpr aligned_allocator(const aligned_allocator<U, Alignment, AllocationPolicy>& /*unused*/) noexcept
    {
    }

    /**
     * @brief Return the largest element count whose byte size is representable.
     *
     * @return `SIZE_MAX / sizeof(T)`.
     */
    [[nodiscard]] static constexpr auto max_size() noexcept -> size_type
    {
        return std::numeric_limits<size_type>::max() / sizeof(value_type);
    }

    /**
     * @brief Allocate uninitialized storage for a number of elements.
     *
     * Multiplication is validated before it is performed. A zero count returns
     * `nullptr` and does not invoke the allocation policy.
     *
     * @param count Number of elements for which storage is requested.
     * @return Aligned raw storage, or `nullptr` when @p count is zero.
     * @throws std::bad_array_new_length When `count * sizeof(T)` would overflow.
     * @throws std::bad_alloc When the allocation policy cannot satisfy the
     * request.
     */
    [[nodiscard]] auto allocate(size_type count) -> value_type*
    {
        if (count == 0) {
            return nullptr;
        }
        // The four uncovered branch instances correspond to the one-byte
        // value types char, signed char, unsigned char, and char8_t. For these
        // types max_size() is SIZE_MAX, so no size_type value can exceed it.
        // Overflow behavior for every wider supported type remains covered.
        if (count > max_size()) { // GCOVR_EXCL_BR_WITHOUT_HIT: 4/38
            throw std::bad_array_new_length{};
        }

        const auto bytes = count * sizeof(value_type);
        return static_cast<value_type*>(AllocationPolicy::allocate_bytes(bytes, Alignment));
    }

    /**
     * @brief Release storage previously returned by @ref allocate.
     *
     * Null pointers are ignored, including the result of `allocate(0)`. For a
     * non-null pointer, @p count must equal the count used for its allocation,
     * as required by the standard allocator contract.
     *
     * @param allocation Pointer returned by this allocator.
     * @param count Original requested element count.
     */
    void deallocate(value_type* allocation, size_type count) noexcept
    {
        if (allocation == nullptr) {
            return;
        }

        AllocationPolicy::deallocate_bytes(allocation, count * sizeof(value_type), Alignment);
    }
};

/**
 * @brief Compare interchangeable aligned allocator instances.
 *
 * @tparam T Left allocator's value type.
 * @tparam U Right allocator's value type.
 * @tparam Alignment Shared byte alignment.
 * @tparam AllocationPolicy Shared stateless allocation policy.
 * @return Always `true` because neither allocator contains state.
 */
template<class T, class U, std::size_t Alignment, class AllocationPolicy>
[[nodiscard]] constexpr auto
operator==(const aligned_allocator<T, Alignment, AllocationPolicy>& /*unused*/,
           const aligned_allocator<U, Alignment, AllocationPolicy>& /*unused*/) noexcept -> bool
{
    return true;
}

} // namespace fieldpack::detail
