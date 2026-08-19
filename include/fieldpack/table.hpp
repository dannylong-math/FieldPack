#pragma once

#include <concepts>
#include <cstddef>
#include <fieldpack/detail/soa_storage.hpp>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

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
    using schema_type = typename unqualified_storage::schema_type;

    /** @brief Unsigned type used for the logical row index. */
    using size_type = typename unqualified_storage::size_type;

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

} // namespace detail

/**
 * @brief Primary declaration for an owning table selected by storage layout.
 *
 * Layout-specific partial specializations provide the implementation. Both
 * template arguments are constrained so invalid schemas and unknown layouts
 * fail before backend machinery is instantiated.
 *
 * @tparam Schema Valid logical field schema.
 * @tparam Layout Recognized storage-layout tag.
 */
template<class Schema, class Layout>
    requires valid_schema<Schema> && valid_layout<Layout>
class table;

/**
 * @brief Own a structure-of-arrays representation of a logical schema.
 *
 * One separately aligned contiguous column is generated for every field.
 * Scalar access returns a small row proxy whose `get<Tag>()` member aliases
 * the corresponding column value. Top-level cv-qualification on @p Schema is
 * accepted and normalized away.
 *
 * @tparam Schema Valid schema, optionally top-level cv-qualified.
 *
 * @code{.cpp}
 * struct x {};
 * struct id {};
 * using particle_schema = fieldpack::schema<
 *     fieldpack::field<x, float>,
 *     fieldpack::field<id, unsigned>>;
 *
 * fieldpack::table<particle_schema, fieldpack::soa> particles(8);
 * particles[2].get<x>() = 3.5F;
 * particles.at(2).get<id>() = 42U;
 * @endcode
 */
template<class Schema>
    requires valid_schema<Schema>
class table<Schema, soa> {
private:
    /** @brief Internal generated column backend. */
    using storage_type = detail::soa_storage<Schema>;

public:
    /** @brief Unqualified logical schema stored by this table. */
    using schema_type = std::remove_cv_t<Schema>;

    /** @brief Layout tag selecting this specialization. */
    using layout_type = soa;

    /** @brief Unsigned type used for element counts and indices. */
    using size_type = std::size_t;

    /** @brief Mutable non-owning logical row reference. */
    using reference = detail::row_proxy<storage_type>;

    /** @brief Immutable non-owning logical row reference. */
    using const_reference = detail::row_proxy<const storage_type>;

    /** @brief Construct an empty table without allocating any column. */
    table() = default;

    /**
     * @brief Construct equally sized, value-initialized field columns.
     *
     * @param count Number of logical records to create.
     * @throws std::bad_array_new_length If a column allocation size overflows.
     * @throws std::bad_alloc If any column allocation cannot be satisfied.
     */
    explicit table(size_type count) : storage_(count) {}

    /** @brief Deep-copy all columns into independent storage. */
    table(const table&) = default;

    /**
     * @brief Move all column allocations and leave the source valid.
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

    /** @brief Destroy all logical values and release every column allocation. */
    ~table() = default;

    /**
     * @brief Return the number of live logical records.
     *
     * @return Shared size of every field column.
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
        return (*this)[index];
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
        return (*this)[index];
    }

    /**
     * @brief Change the logical record count of every field column.
     *
     * The first `min(old_size, new_size)` records are preserved. Newly live
     * fields are value-initialized. A failed growth leaves this table's size,
     * values, and allocations unchanged. Every call invalidates previously
     * obtained row proxies by contract, including a same-size call.
     *
     * @param new_size Requested logical record count.
     * @throws std::bad_array_new_length If a replacement allocation size
     * overflows.
     * @throws std::bad_alloc If any replacement column cannot be allocated.
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
            throw std::out_of_range{"fieldpack::table::at index out of range"};
        }
    }

    /** @brief Generated aligned columns holding every logical field value. */
    storage_type storage_{};
};

} // namespace fieldpack
