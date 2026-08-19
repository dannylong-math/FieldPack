#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide test-header convention

#include <cstddef>
#include <new>

namespace fieldpack_test {

/**
 * Test-only raw allocation policy used to observe ownership and inject a
 * deterministic allocation failure. Production code must not depend on it.
 */
struct tracking_allocation_policy {
    static inline std::size_t allocation_attempts{};
    static inline std::size_t successful_allocations{};
    static inline std::size_t deallocations{};
    static inline std::size_t live_allocations{};
    static inline std::size_t fail_on_attempt{};

    static void reset() noexcept
    {
        allocation_attempts = 0;
        successful_allocations = 0;
        deallocations = 0;
        live_allocations = 0;
        fail_on_attempt = 0;
    }

    [[nodiscard]] static auto allocate_bytes(std::size_t bytes, std::size_t alignment) -> void*
    {
        ++allocation_attempts;
        if (fail_on_attempt != 0 && allocation_attempts == fail_on_attempt) {
            throw std::bad_alloc{};
        }

        auto* allocation = ::operator new(bytes, std::align_val_t{alignment});
        ++successful_allocations;
        ++live_allocations;
        return allocation;
    }

    static void deallocate_bytes(void* allocation, std::size_t /*unused*/, std::size_t alignment) noexcept
    {
        if (allocation == nullptr) {
            return;
        }

        ::operator delete(allocation, std::align_val_t{alignment});
        ++deallocations;
        --live_allocations;
    }
};

} // namespace fieldpack_test
