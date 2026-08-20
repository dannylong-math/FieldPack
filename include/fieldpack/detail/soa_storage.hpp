#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide header convention

#include <cstddef>
#include <fieldpack/detail/field_storage.hpp>
#include <fieldpack/schema.hpp>
#include <span>
#include <type_traits>
#include <utility>

/**
 * @file soa_storage.hpp
 * @brief Internal structure-of-arrays storage generated from a schema.
 */

namespace fieldpack::detail {

/**
 * @brief Own the aligned contiguous values for one schema field.
 *
 * The complete field descriptor appears in the base-class type, so fields
 * with equal value types still receive distinct bases. This lets
 * @ref soa_storage_impl select a column by tag without exposing tuple
 * positions.
 *
 * @tparam Field Valid unqualified field descriptor.
 * @tparam AllocationPolicy Stateless raw allocation policy used by the
 * column's aligned allocator.
 */
template<class Field, class AllocationPolicy>
    requires valid_field<Field> && stateless_allocation_policy<AllocationPolicy>
class soa_column {
private:
    /** @brief Metadata extracted from the field descriptor. */
    using traits = field_traits<Field>;

public:
    /** @brief Tag naming this column. */
    using tag_type = traits::tag;

    /** @brief Arithmetic value stored in this column. */
    using value_type = traits::type;

    /** @brief Construct an empty column without allocating. */
    soa_column() = default;

    /**
     * @brief Construct a value-initialized column.
     *
     * @param count Number of values to create.
     * @throws std::bad_array_new_length If the allocation size overflows.
     * @throws std::bad_alloc If the allocation cannot be satisfied.
     */
    explicit soa_column(std::size_t count) : values_(count) {}

    /** @brief Copy this column and its values into independent storage. */
    soa_column(const soa_column&) = default;

    /** @brief Move this column's allocation without copying its values. */
    soa_column(soa_column&&) noexcept = default;

    /** @brief Copy-assign this column using its aligned vector semantics. */
    auto operator=(const soa_column&) -> soa_column& = default;

    /** @brief Move-assign this column's allocation. */
    auto operator=(soa_column&&) noexcept -> soa_column& = default;

    /** @brief Destroy every value and release the column allocation. */
    ~soa_column() = default;

    /**
     * @brief Access one mutable value without bounds checking.
     *
     * @param index Valid zero-based element index.
     * @return Reference to the stored field value.
     */
    [[nodiscard]] auto element(std::size_t index) noexcept -> value_type&
    {
        return values_[index]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }

    /**
     * @brief Access one immutable value without bounds checking.
     *
     * @param index Valid zero-based element index.
     * @return Const reference to the stored field value.
     */
    [[nodiscard]] auto element(std::size_t index) const noexcept -> const value_type&
    {
        return values_[index]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }

    /**
     * @brief Form a mutable span over a validated contiguous subrange.
     *
     * @tparam Extent Static span extent or `std::dynamic_extent`.
     * @param first First element in the column subrange.
     * @param count Number of elements in the subrange.
     * @return Mutable span over exactly @p count values.
     */
    template<std::size_t Extent>
    [[nodiscard]] auto span(std::size_t first, std::size_t count) noexcept -> std::span<value_type, Extent>
    {
        return std::span<value_type, Extent>{std::span<value_type>{values_}.subspan(first, count)};
    }

    /**
     * @brief Form an immutable span over a validated contiguous subrange.
     *
     * @tparam Extent Static span extent or `std::dynamic_extent`.
     * @param first First element in the column subrange.
     * @param count Number of elements in the subrange.
     * @return Const span over exactly @p count values.
     */
    template<std::size_t Extent>
    [[nodiscard]] auto span(std::size_t first, std::size_t count) const noexcept -> std::span<const value_type, Extent>
    {
        return std::span<const value_type, Extent>{std::span<const value_type>{values_}.subspan(first, count)};
    }

    /**
     * @brief Reduce the column to a smaller size.
     *
     * The caller guarantees that @p count does not exceed the current size.
     * Supported field values have trivial non-throwing destruction, so this
     * operation cannot encounter allocation failure.
     *
     * @param count New smaller element count.
     */
    void shrink(std::size_t count) { values_.resize(count); }

    /**
     * @brief Copy a prefix from another equal-size-or-larger column.
     *
     * Destination values already exist and arithmetic assignment cannot
     * throw. The helper is used only while preparing replacement storage.
     *
     * @param source Column whose prefix is copied.
     * @param count Number of leading values to copy.
     */
    void copy_prefix_from(const soa_column& source, std::size_t count) noexcept
    {
        for (std::size_t index = 0; index < count; ++index) {
            // Both columns were sized before this validated prefix is copied.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            values_[index] = source.values_[index];
        }
    }

    /**
     * @brief Exchange allocations with another column.
     *
     * @param other Column receiving this allocation.
     */
    void swap(soa_column& other) noexcept { values_.swap(other.values_); }

    /** @brief Remove every live value while retaining optional capacity. */
    void clear() noexcept { values_.clear(); }

private:
    /** @brief Contiguous aligned allocation holding this field's values. */
    field_storage<value_type, AllocationPolicy> values_{};
};

/**
 * @brief Primary declaration adapting SoA storage to a schema field pack.
 *
 * @tparam Schema Unqualified schema specialization.
 * @tparam AllocationPolicy Stateless raw allocation policy.
 */
template<class Schema, class AllocationPolicy> class soa_storage_impl;

/**
 * @brief Generate one aligned column base for every schema field.
 *
 * Column bases are private representation details. Named @ref element access
 * resolves a tag to its field descriptor and then selects the matching base.
 * A single logical size is updated only after every column operation that can
 * fail has succeeded.
 *
 * @tparam Fields Valid schema field descriptors.
 * @tparam AllocationPolicy Stateless raw allocation policy shared by all
 * columns.
 */
template<class... Fields, class AllocationPolicy>
    requires stateless_allocation_policy<AllocationPolicy>
// One private base per uniquely tagged field provides compile-time named
// selection without exposing positional tuple access.
// NOLINTNEXTLINE(misc-multiple-inheritance)
class soa_storage_impl<schema<Fields...>, AllocationPolicy> : private soa_column<Fields, AllocationPolicy>... {
private:
    /** @brief Column type associated with one field descriptor. */
    template<class Field> using column_type = soa_column<Field, AllocationPolicy>;

    /** @brief Field descriptor selected by an exact tag lookup. */
    template<class Tag> using selected_field = field_type_impl<schema<Fields...>, Tag>::selected_field;

    /** @brief Column base selected by an exact tag lookup. */
    template<class Tag> using selected_column = column_type<selected_field<Tag>>;

public:
    /** @brief Unqualified schema represented by this backend. */
    using schema_type = schema<Fields...>;

    /** @brief Raw allocation policy shared by every field column. */
    using allocation_policy = AllocationPolicy;

    /** @brief Unsigned type used for logical element counts and indices. */
    using size_type = std::size_t;

    /** @brief Construct empty storage without allocating any column. */
    soa_storage_impl() = default;

    /**
     * @brief Construct equally sized, value-initialized field columns.
     *
     * If a later column allocation fails, C++ base construction destroys all
     * earlier columns before propagating the exception.
     *
     * @param count Logical record count assigned to every column.
     * @throws std::bad_array_new_length If a column allocation size overflows.
     * @throws std::bad_alloc If any column allocation cannot be satisfied.
     */
    // This source line contains only compiler-generated cleanup edges between
    // column constructors. Injected-policy tests fail at every column and cover
    // the cleanup behavior without relying on each test executable's duplicate
    // inline production instantiations.
    explicit soa_storage_impl(size_type count) : column_type<Fields>(count)..., size_(count) // GCOVR_EXCL_BR_LINE
    {
    }

    /**
     * @brief Deep-copy every field column.
     */
    //  Branches attributed to this declaration are compiler-generated cleanup
    //  edges. Injected-policy tests fail each column copy in turn and cover the
    //  equivalent cleanup behavior.
    soa_storage_impl(const soa_storage_impl&) = default; // GCOVR_EXCL_BR_LINE

    /**
     * @brief Move every column and leave the source empty and consistent.
     *
     * @param other Backend whose allocations are transferred.
     */
    soa_storage_impl(soa_storage_impl&& other) noexcept :
        column_type<Fields>(std::move(static_cast<column_type<Fields>&>(other)))...,
        size_(std::exchange(other.size_, 0))
    {
        other.clear_columns();
    }

    /**
     * @brief Deep-copy another backend with the strong exception guarantee.
     *
     * @param other Backend to copy.
     * @return This backend after replacement.
     */
    auto operator=(const soa_storage_impl& other) -> soa_storage_impl&
    {
        if (this != &other) {
            soa_storage_impl replacement(other);
            swap(replacement);
        }
        return *this;
    }

    /**
     * @brief Move-assign another backend without allocating.
     *
     * @param other Backend whose allocations are transferred.
     * @return This backend after replacement.
     */
    auto operator=(soa_storage_impl&& other) noexcept -> soa_storage_impl&
    {
        if (this != &other) {
            soa_storage_impl replacement(std::move(other));
            swap(replacement);
        }
        return *this;
    }

    /** @brief Destroy every column and its live values. */
    ~soa_storage_impl() = default;

    /**
     * @brief Return the shared logical size of every column.
     *
     * @return Number of live logical records.
     */
    [[nodiscard]] auto size() const noexcept -> size_type { return size_; }

    /**
     * @brief Determine whether the backend contains no logical records.
     *
     * @return `true` when @ref size is zero.
     */
    [[nodiscard]] auto empty() const noexcept -> bool { return size_ == 0; }

    /**
     * @brief Access one mutable field value by exact tag.
     *
     * The operation is unchecked; @p index must be less than @ref size.
     *
     * @tparam Tag Tag present in @ref schema_type.
     * @param index Valid zero-based logical index.
     * @return Mutable reference to the named field value.
     */
    template<class Tag>
        requires contains_tag_v<schema_type, Tag>
    [[nodiscard]] auto element(size_type index) noexcept -> field_type_t<schema_type, Tag>&
    {
        return static_cast<selected_column<Tag>&>(*this).element(index);
    }

    /**
     * @brief Access one immutable field value by exact tag.
     *
     * The operation is unchecked; @p index must be less than @ref size.
     *
     * @tparam Tag Tag present in @ref schema_type.
     * @param index Valid zero-based logical index.
     * @return Const reference to the named field value.
     */
    template<class Tag>
        requires contains_tag_v<schema_type, Tag>
    [[nodiscard]] auto element(size_type index) const noexcept -> const field_type_t<schema_type, Tag>&
    {
        return static_cast<const selected_column<Tag>&>(*this).element(index);
    }

    /**
     * @brief Return the live contiguous count beginning at an index.
     *
     * Every SoA field is one complete logical column. The caller supplies an
     * index no greater than @ref size.
     *
     * @param index Logical starting index.
     * @return Number of live values from @p index through the column end.
     */
    [[nodiscard]] auto contiguous_count(size_type index) const noexcept -> size_type { return size_ - index; }

    /**
     * @brief Form a mutable named-field span within one contiguous region.
     *
     * The execution layer guarantees `first <= size()` and
     * `count <= contiguous_count(first)`.
     *
     * @tparam Tag Exact tag present in @ref schema_type.
     * @tparam Extent Static span extent or `std::dynamic_extent`.
     * @param first First logical index in the span.
     * @param count Number of live values exposed by the span.
     * @return Mutable span over the named SoA column.
     */
    template<class Tag, std::size_t Extent = std::dynamic_extent>
        requires contains_tag_v<schema_type, Tag>
    [[nodiscard]] auto contiguous_span(size_type first,
                                       size_type count) noexcept -> std::span<field_type_t<schema_type, Tag>, Extent>
    {
        return static_cast<selected_column<Tag>&>(*this).template span<Extent>(first, count);
    }

    /**
     * @brief Form an immutable named-field span within one contiguous region.
     *
     * The execution layer guarantees `first <= size()` and
     * `count <= contiguous_count(first)`.
     *
     * @tparam Tag Exact tag present in @ref schema_type.
     * @tparam Extent Static span extent or `std::dynamic_extent`.
     * @param first First logical index in the span.
     * @param count Number of live values exposed by the span.
     * @return Const span over the named SoA column.
     */
    template<class Tag, std::size_t Extent = std::dynamic_extent>
        requires contains_tag_v<schema_type, Tag>
    [[nodiscard]] auto contiguous_span(size_type first, size_type count) const noexcept
        -> std::span<const field_type_t<schema_type, Tag>, Extent>
    {
        return static_cast<const selected_column<Tag>&>(*this).template span<Extent>(first, count);
    }

    /**
     * @brief Change every column to the same logical size.
     *
     * Shrinking cannot allocate and is applied directly to every column.
     * Growing first creates value-initialized replacement storage, copies the
     * preserved prefix, and swaps only after all allocations succeed. A failed
     * growth therefore leaves this backend's size, values, and allocations
     * unchanged.
     *
     * @param new_size Requested logical record count.
     * @throws std::bad_array_new_length If a replacement allocation size
     * overflows.
     * @throws std::bad_alloc If a replacement column cannot be allocated.
     */
    void resize(size_type new_size)
    {
        if (new_size == size_) {
            return;
        }

        if (new_size < size_) {
            (static_cast<column_type<Fields>&>(*this).shrink(new_size), ...);
            size_ = new_size;
            return;
        }

        soa_storage_impl replacement(new_size);
        (static_cast<column_type<Fields>&>(replacement)
             .copy_prefix_from(static_cast<const column_type<Fields>&>(*this), size_),
         ...);
        swap(replacement);
    }

    /**
     * @brief Exchange complete backend states without allocating.
     *
     * @param other Backend receiving this state.
     */
    void swap(soa_storage_impl& other) noexcept
    {
        (static_cast<column_type<Fields>&>(*this).swap(static_cast<column_type<Fields>&>(other)), ...);
        std::swap(size_, other.size_);
    }

private:
    /** @brief Clear every column while preserving the zero logical size. */
    void clear_columns() noexcept { (static_cast<column_type<Fields>&>(*this).clear(), ...); }

    /** @brief Shared live-record count for all generated columns. */
    size_type size_{};
};

/**
 * @brief Internal SoA backend for a valid schema.
 *
 * Top-level schema cv-qualification is normalized away. The allocation-policy
 * parameter is an internal test seam; production tables use
 * @ref aligned_new_policy.
 *
 * @tparam Schema Valid schema, optionally top-level cv-qualified.
 * @tparam AllocationPolicy Stateless raw allocation policy shared by every
 * field column.
 */
template<class Schema, class AllocationPolicy = aligned_new_policy>
    requires valid_schema<Schema> && stateless_allocation_policy<AllocationPolicy>
using soa_storage = soa_storage_impl<std::remove_cv_t<Schema>, AllocationPolicy>;

} // namespace fieldpack::detail
