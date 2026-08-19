#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide header convention

#include <cstddef>
#include <fieldpack/detail/aosoa_storage.hpp>
#include <fieldpack/detail/soa_storage.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <memory>
#include <stdexcept>
#include <type_traits>

/**
 * @file table.hpp
 * @brief Owning named-field tables and non-owning scalar row proxies.
 */

namespace fieldpack {

/**
 * @brief Internal scalar-access machinery shared by table layouts.
 */
namespace detail {

/**
 * @brief Throw the common exception for an invalid checked table index.
 *
 * Keeping exception construction outside the templated table facade avoids
 * duplicating its cold allocation and unwinding path for every schema/layout
 * combination.
 *
 * @throws std::out_of_range Unconditionally.
 */
[[noreturn]] inline void throw_table_index_out_of_range()
{
    // The uncovered branch is allocation failure inside the standard
    // exception constructor, which cannot be injected through the table API.
    throw std::out_of_range{"fieldpack::table::at index out of range"}; // GCOVR_EXCL_BR_WITHOUT_HIT: 1/2
} // GCOVR_EXCL_LINE -- unreachable because the function always throws

/**
 * @brief Non-owning view of one logical record in a storage backend.
 *
 * Proxy constness does not change field access. Instead, the cv-qualification
 * of @p Storage records whether the originating table was mutable or const.
 * Like `std::span<T>`, a const proxy into mutable storage therefore still
 * returns mutable references.
 *
 * The proxy contains only a backend pointer and logical index. Resizing,
 * assigning, moving, swapping, or destroying the source table invalidates it.
 *
 * @tparam Storage Mutable or const storage backend type.
 */
template<class Storage> class row_proxy {
private:
    /** @brief Backend type with top-level cv-qualification removed. */
    using unqualified_storage = std::remove_cv_t<Storage>;

public:
    /** @brief Schema whose tags may be accessed through this proxy. */
    using schema_type = unqualified_storage::schema_type;

    /** @brief Unsigned type used for the logical row index. */
    using size_type = unqualified_storage::size_type;

    /**
     * @brief Bind a proxy to one logical backend index.
     *
     * @param storage Backend that owns the field values.
     * @param index Valid logical element index.
     */
    constexpr row_proxy(Storage& storage, size_type index) noexcept : storage_(std::addressof(storage)), index_(index)
    {
    }

    /**
     * @brief Access a field in this logical record by exact tag.
     *
     * The returned reference is mutable exactly when @p Storage is mutable.
     * The member itself is const because changing the small proxy object does
     * not govern mutation of the referenced table.
     *
     * @tparam Tag Exact tag present in @ref schema_type.
     * @return Reference to the named field at this proxy's index.
     *
     * @code{.cpp}
     * auto row = particles[3];
     * row.get<x>() = 1.0F;
     *
     * const auto& observed = particles;
     * const float& value = observed[3].get<x>();
     * @endcode
     */
    template<class Tag>
        requires contains_tag_v<schema_type, Tag>
    [[nodiscard]] constexpr decltype(auto) get() const noexcept
    {
        return storage_->template element<Tag>(index_);
    }

private:
    /** @brief Backend owning the referenced values. */
    Storage* storage_;

    /** @brief Logical record selected in the backend. */
    size_type index_;
};

/**
 * @brief Primary declaration mapping a public layout to its storage backend.
 *
 * Recognized layout specializations keep backend selection separate from the
 * common owning table facade.
 *
 * @tparam Schema Valid logical field schema.
 * @tparam Layout Recognized storage-layout tag.
 */
template<class Schema, class Layout> struct table_storage;

/** @brief Select generated column storage for the SoA layout. */
template<class Schema> struct table_storage<Schema, soa> {
    /** @brief Internal structure-of-arrays backend. */
    using type = soa_storage<Schema>;
};

/** @brief Select generated complete-tile storage for an AoSoA layout. */
template<class Schema, std::size_t TileExtent> struct table_storage<Schema, aosoa<TileExtent>> {
    /** @brief Internal homogeneous AoSoA backend. */
    using type = aosoa_storage<Schema, TileExtent>;
};

/**
 * @brief Resolve the internal backend associated with a public layout.
 *
 * @tparam Schema Valid logical field schema.
 * @tparam Layout Recognized storage-layout tag.
 */
template<class Schema, class Layout> using table_storage_t = table_storage<Schema, Layout>::type;

} // namespace detail

/**
 * @brief Own a named-field table using a selected physical storage layout.
 *
 * The facade provides identical scalar semantics for every recognized layout.
 * An internal selector supplies either separately allocated SoA columns or a
 * single allocation of complete AoSoA tiles. Scalar access returns a small row
 * proxy whose `get<Tag>()` member aliases the corresponding backend value.
 * Top-level cv-qualification on @p Schema is accepted and normalized away.
 *
 * @tparam Schema Valid schema, optionally top-level cv-qualified.
 * @tparam Layout Recognized storage-layout tag.
 *
 * @code{.cpp}
 * struct x {};
 * struct id {};
 * using particle_schema = fieldpack::schema<
 *     fieldpack::field<x, float>,
 *     fieldpack::field<id, unsigned>>;
 *
 * fieldpack::table<particle_schema, fieldpack::aosoa<64>> particles(8);
 * particles[2].get<x>() = 3.5F;
 * particles.at(2).get<id>() = 42U;
 * @endcode
 */
template<class Schema, class Layout>
    requires valid_schema<Schema> && valid_layout<Layout>
class table {
private:
    /** @brief Internal backend selected from the public layout tag. */
    using storage_type = detail::table_storage_t<Schema, Layout>;

public:
    /** @brief Unqualified logical schema stored by this table. */
    using schema_type = std::remove_cv_t<Schema>;

    /** @brief Layout tag selecting the physical storage backend. */
    using layout_type = Layout;

    /** @brief Unsigned type used for element counts and indices. */
    using size_type = std::size_t;

    /** @brief Mutable non-owning logical row reference. */
    using reference = detail::row_proxy<storage_type>;

    /** @brief Immutable non-owning logical row reference. */
    using const_reference = detail::row_proxy<const storage_type>;

    /** @brief Construct an empty table without allocating backend storage. */
    table() = default;

    /**
     * @brief Construct value-initialized storage for logical records.
     *
     * @param count Number of logical records to create.
     * @throws std::bad_array_new_length If a backend allocation size
     * overflows.
     * @throws std::bad_alloc If backend storage cannot be allocated.
     */
    explicit table(size_type count) : storage_(count) {}

    /** @brief Deep-copy all logical values into independent storage. */
    table(const table&) = default;

    /**
     * @brief Move backend allocations and leave the source valid.
     *
     * The moved-from table remains destructible and assignable. Its precise
     * observable state is intentionally not part of the public contract.
     */
    table(table&&) noexcept = default;

    /**
     * @brief Deep-copy another table with the backend's strong guarantee.
     *
     * @return This table after replacement.
     */
    auto operator=(const table&) -> table& = default;

    /**
     * @brief Move-assign another table without copying field values.
     *
     * @return This table after replacement.
     */
    auto operator=(table&&) noexcept -> table& = default;

    /** @brief Destroy all logical values and release backend allocations. */
    ~table() = default;

    /**
     * @brief Return the number of live logical records.
     *
     * @return Logical size independent of physical layout or padding.
     */
    [[nodiscard]] auto size() const noexcept -> size_type { return storage_.size(); }

    /**
     * @brief Determine whether the table contains no logical records.
     *
     * @return `true` when @ref size is zero.
     */
    [[nodiscard]] auto empty() const noexcept -> bool { return storage_.empty(); }

    /**
     * @brief Return an unchecked mutable proxy for one valid logical index.
     *
     * @param index Index less than @ref size.
     * @return Mutable proxy aliasing the selected logical record.
     */
    [[nodiscard]] auto operator[](size_type index) noexcept -> reference { return reference{storage_, index}; }

    /**
     * @brief Return an unchecked immutable proxy for one valid logical index.
     *
     * @param index Index less than @ref size.
     * @return Immutable proxy aliasing the selected logical record.
     */
    [[nodiscard]] auto operator[](size_type index) const noexcept -> const_reference
    {
        return const_reference{storage_, index};
    }

    /**
     * @brief Return a bounds-checked mutable row proxy.
     *
     * @param index Requested logical index.
     * @return Mutable proxy for @p index.
     * @throws std::out_of_range When @p index is not less than @ref size.
     */
    [[nodiscard]] auto at(size_type index) -> reference
    {
        check_index(index);
        return reference{storage_, index};
    }

    /**
     * @brief Return a bounds-checked immutable row proxy.
     *
     * @param index Requested logical index.
     * @return Immutable proxy for @p index.
     * @throws std::out_of_range When @p index is not less than @ref size.
     */
    [[nodiscard]] auto at(size_type index) const -> const_reference
    {
        check_index(index);
        return const_reference{storage_, index};
    }

    /**
     * @brief Change the logical record count of the selected backend.
     *
     * The first `min(old_size, new_size)` records are preserved. Newly live
     * fields are value-initialized. A failed growth leaves this table's size,
     * values, and allocations unchanged. Every call invalidates previously
     * obtained row proxies by contract, including a same-size call.
     *
     * @param new_size Requested logical record count.
     * @throws std::bad_array_new_length If a replacement allocation size
     * overflows.
     * @throws std::bad_alloc If replacement backend storage cannot be
     * allocated.
     */
    void resize(size_type new_size) { storage_.resize(new_size); }

private:
    /**
     * @brief Reject an index outside the current logical range.
     *
     * @param index Index supplied to a checked accessor.
     * @throws std::out_of_range When @p index is not less than @ref size.
     */
    void check_index(size_type index) const
    {
        if (index >= size()) {
            detail::throw_table_index_out_of_range();
        }
    }

    /** @brief Layout-selected storage holding every logical field value. */
    storage_type storage_{};
};

} // namespace fieldpack
