#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide header convention

#include <algorithm>
#include <array>
#include <cstddef>
#include <fieldpack/detail/field_storage.hpp>
#include <fieldpack/schema.hpp>
#include <new>
#include <type_traits>
#include <utility>

/**
 * @file aosoa_storage.hpp
 * @brief Internal homogeneous AoSoA storage generated from a schema.
 */

namespace fieldpack::detail {

/**
 * @brief Own one field's fixed-size array inside a physical tile.
 *
 * Every base begins at the common layout-independent alignment. The complete
 * field descriptor participates in the base type, so equal value types with
 * different tags remain independently selectable.
 *
 * @tparam Field Valid unqualified field descriptor.
 * @tparam TileExtent Positive number of values in the physical field array.
 */
template<class Field, std::size_t TileExtent>
    requires valid_field<Field> && (TileExtent > 0U)
struct alignas(default_alignment) tile_field_storage {
    /** @brief Arithmetic value stored for this field. */
    using value_type = field_traits<Field>::type;

    /** @brief Value-initialized physical lanes, including logical padding. */
    std::array<value_type, TileExtent> values{};
};

/**
 * @brief Combine every schema field array into one complete physical tile.
 *
 * Public inheritance is confined to this detail type and permits the backend
 * to select a field array by its complete field descriptor. Raw tile objects
 * are never exposed by the public table API.
 *
 * @tparam TileExtent Positive number of values per field.
 * @tparam Fields Valid schema field descriptors with unique tags.
 */
template<std::size_t TileExtent, class... Fields>
    requires(TileExtent > 0U) && (valid_field<Fields> && ...)
// One base per uniquely tagged field provides compile-time named selection.
// NOLINTNEXTLINE(misc-multiple-inheritance)
struct tile_storage : tile_field_storage<Fields, TileExtent>... {};

/**
 * @brief Primary declaration adapting AoSoA storage to a schema field pack.
 *
 * @tparam Schema Unqualified schema specialization.
 * @tparam TileExtent Positive homogeneous physical tile extent.
 * @tparam AllocationPolicy Stateless raw allocation policy.
 */
template<class Schema, std::size_t TileExtent, class AllocationPolicy> class aosoa_storage_impl;

/**
 * @brief Own generated complete tiles and a separate logical record count.
 *
 * A single aligned vector-like allocation contains all physical tiles. Named
 * scalar access maps a logical index to `index / TileExtent` and
 * `index % TileExtent`, then selects the field base associated with the exact
 * schema tag. Complete final-tile padding remains an internal implementation
 * detail.
 *
 * @tparam TileExtent Positive homogeneous physical tile extent.
 * @tparam AllocationPolicy Stateless raw allocation policy for the tile block.
 * @tparam Fields Valid schema field descriptors with unique tags.
 */
template<std::size_t TileExtent, class AllocationPolicy, class... Fields>
    requires(TileExtent > 0U) && stateless_allocation_policy<AllocationPolicy>
class aosoa_storage_impl<schema<Fields...>, TileExtent, AllocationPolicy> {
private:
    /** @brief Complete generated physical tile type. */
    using tile_type = tile_storage<TileExtent, Fields...>;

    /** @brief Single aligned allocation holding every complete tile. */
    using tile_container = field_storage<tile_type, AllocationPolicy>;

    /** @brief Field descriptor selected by an exact tag lookup. */
    template<class Tag> using selected_field = field_type_impl<schema<Fields...>, Tag>::selected_field;

    /** @brief Fixed-size tile field base selected by an exact tag lookup. */
    template<class Field> using tile_field_type = tile_field_storage<Field, TileExtent>;

    /** @brief Selected tile field base associated with an exact tag. */
    template<class Tag> using selected_tile_field = tile_field_type<selected_field<Tag>>;

public:
    /** @brief Unqualified schema represented by this backend. */
    using schema_type = schema<Fields...>;

    /** @brief Raw allocation policy used by the complete tile block. */
    using allocation_policy = AllocationPolicy;

    /** @brief Unsigned type used for logical sizes, tile counts, and indices. */
    using size_type = std::size_t;

    /** @brief Compile-time physical field-array extent. */
    static constexpr size_type tile_extent = TileExtent;

    /** @brief Construct empty storage without allocating a physical tile. */
    aosoa_storage_impl() = default;

    /**
     * @brief Construct enough complete value-initialized tiles for a size.
     *
     * @param count Logical record count.
     * @throws std::bad_array_new_length If the required tile allocation is not
     * representable.
     * @throws std::bad_alloc If the complete tile block cannot be allocated.
     */
    explicit aosoa_storage_impl(size_type count) { resize(count); }

    /** @brief Deep-copy every complete tile and the logical size. */
    aosoa_storage_impl(const aosoa_storage_impl&) = default;

    /**
     * @brief Move the tile block and leave the source consistently empty.
     *
     * @param other Backend whose complete tile allocation is transferred.
     */
    aosoa_storage_impl(aosoa_storage_impl&& other) noexcept :
        tiles_(std::move(other.tiles_)), logical_size_(std::exchange(other.logical_size_, 0U))
    {
        other.tiles_.clear();
    }

    /**
     * @brief Deep-copy another backend with the strong exception guarantee.
     *
     * @param other Backend to copy.
     * @return This backend after replacement.
     */
    auto operator=(const aosoa_storage_impl& other) -> aosoa_storage_impl&
    {
        if (this != &other) {
            aosoa_storage_impl replacement(other);
            swap(replacement);
        }
        return *this;
    }

    /**
     * @brief Move-assign another backend without copying field values.
     *
     * @param other Backend whose complete tile allocation is transferred.
     * @return This backend after replacement.
     */
    auto operator=(aosoa_storage_impl&& other) noexcept -> aosoa_storage_impl&
    {
        if (this != &other) {
            aosoa_storage_impl replacement(std::move(other));
            swap(replacement);
        }
        return *this;
    }

    /** @brief Destroy all tiles and their arithmetic field values. */
    ~aosoa_storage_impl() = default;

    /**
     * @brief Return the number of live logical records.
     *
     * @return Logical size excluding final-tile padding.
     */
    [[nodiscard]] auto size() const noexcept -> size_type { return logical_size_; }

    /**
     * @brief Determine whether no logical records are live.
     *
     * @return `true` when @ref size is zero.
     */
    [[nodiscard]] auto empty() const noexcept -> bool { return logical_size_ == 0U; }

    /**
     * @brief Return the number of internally allocated complete tiles.
     *
     * This observer exists for detail-level backend verification. Public table
     * users intentionally cannot obtain tile objects or physical padding.
     *
     * @return Complete physical tile count.
     */
    [[nodiscard]] auto physical_tile_count() const noexcept -> size_type { return tiles_.size(); }

    /**
     * @brief Access one mutable logical field value by exact tag.
     *
     * The operation is unchecked; @p index must be less than @ref size.
     *
     * @tparam Tag Exact tag present in @ref schema_type.
     * @param index Valid zero-based logical index.
     * @return Mutable reference to the mapped tile field lane.
     */
    template<class Tag>
        requires contains_tag_v<schema_type, Tag>
    [[nodiscard]] auto element(size_type index) noexcept -> field_type_t<schema_type, Tag>&
    {
        // The public facade and backend contract require a live logical index.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        auto& tile = tiles_[tile_index(index)];
        return field_lane<Tag>(tile, lane_index(index));
    }

    /**
     * @brief Access one immutable logical field value by exact tag.
     *
     * The operation is unchecked; @p index must be less than @ref size.
     *
     * @tparam Tag Exact tag present in @ref schema_type.
     * @param index Valid zero-based logical index.
     * @return Const reference to the mapped tile field lane.
     */
    template<class Tag>
        requires contains_tag_v<schema_type, Tag>
    [[nodiscard]] auto element(size_type index) const noexcept -> const field_type_t<schema_type, Tag>&
    {
        // The public facade and backend contract require a live logical index.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        const auto& tile = tiles_[tile_index(index)];
        return field_lane<Tag>(tile, lane_index(index));
    }

    /**
     * @brief Change the logical size while retaining complete physical tiles.
     *
     * The first `min(size(), new_size)` records are preserved. Newly allocated
     * tiles are value-initialized. When shrinking leaves a partial tile, lanes
     * that cease to be live are cleared before they can become padding. A grow
     * that allocates and fails leaves the original size and values unchanged.
     *
     * @param new_size Requested logical record count.
     * @throws std::bad_array_new_length If the required tile allocation is not
     * representable.
     * @throws std::bad_alloc If a larger complete tile block cannot be
     * allocated.
     */
    void resize(size_type new_size)
    {
        if (new_size == logical_size_) {
            return;
        }

        const auto new_tile_count = checked_tile_count(new_size);
        if (new_size < logical_size_) {
            if constexpr (TileExtent > 1U) {
                clear_retained_stale_lanes(new_size);
            }
            tiles_.resize(new_tile_count);
            logical_size_ = new_size;
            return;
        }

        const auto old_size = logical_size_;
        tiles_.resize(new_tile_count);
        if constexpr (TileExtent > 1U) {
            clear_previous_padding(old_size, new_size);
        }
        logical_size_ = new_size;
    }

    /**
     * @brief Exchange complete backend states without allocating.
     *
     * @param other Backend receiving this tile block and logical size.
     */
    void swap(aosoa_storage_impl& other) noexcept
    {
        tiles_.swap(other.tiles_);
        std::swap(logical_size_, other.logical_size_);
    }

private:
    /** @brief Map a logical index to its zero-based physical tile index. */
    [[nodiscard]] static constexpr auto tile_index(size_type index) noexcept -> size_type { return index / TileExtent; }

    /** @brief Map a logical index to its lane in the containing tile. */
    [[nodiscard]] static constexpr auto lane_index(size_type index) noexcept -> size_type { return index % TileExtent; }

    /**
     * @brief Compute and validate the number of complete required tiles.
     *
     * Division followed by a remainder increment avoids the overflow inherent
     * in `(logical_size + TileExtent - 1) / TileExtent`.
     *
     * @param logical_size Requested live record count.
     * @return Checked complete physical tile count.
     * @throws std::bad_array_new_length If the vector cannot represent the tile
     * count.
     */
    [[nodiscard]] auto checked_tile_count(size_type logical_size) const -> size_type
    {
        const auto result = (logical_size / TileExtent) + static_cast<size_type>(logical_size % TileExtent != 0U);
        if (result > tiles_.max_size()) {
            throw std::bad_array_new_length{};
        }
        return result;
    }

    /** @brief Select one mutable named field lane from a complete tile. */
    template<class Tag>
    [[nodiscard]] static auto field_lane(tile_type& tile, size_type lane) noexcept -> field_type_t<schema_type, Tag>&
    {
        auto& field_values = static_cast<selected_tile_field<Tag>&>(tile).values;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)
        return field_values[lane];
    }

    /** @brief Select one immutable named field lane from a complete tile. */
    template<class Tag>
    [[nodiscard]] static auto field_lane(const tile_type& tile, size_type lane) noexcept
        -> const field_type_t<schema_type, Tag>&
    {
        const auto& field_values = static_cast<const selected_tile_field<Tag>&>(tile).values;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)
        return field_values[lane];
    }

    /** @brief Value-initialize one field lane selected by its descriptor. */
    template<class Field> static void clear_field_lane(tile_type& tile, size_type lane) noexcept
    {
        auto& field_values = static_cast<tile_field_type<Field>&>(tile).values;
        // Resize helpers pass a lane strictly below the fixed tile extent.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index)
        field_values[lane] = typename field_traits<Field>::type{};
    }

    /** @brief Value-initialize every field at one allocated physical lane. */
    void clear_lane(size_type index) noexcept
    {
        // The caller restricts clearing to currently allocated complete tiles.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        auto& tile = tiles_[tile_index(index)];
        (clear_field_lane<Fields>(tile, lane_index(index)), ...);
    }

    /** @brief Value-initialize an allocated half-open logical-index range. */
    void clear_range(size_type first, size_type last) noexcept
    {
        for (auto index = first; index < last; ++index) {
            clear_lane(index);
        }
    }

    /** @brief Clear newly dead lanes that remain in a retained partial tile. */
    void clear_retained_stale_lanes(size_type new_size) noexcept
    {
        const auto lane = lane_index(new_size);
        if (lane == 0U) {
            return;
        }

        const auto stale_count = std::min(logical_size_ - new_size, TileExtent - lane);
        clear_range(new_size, new_size + stale_count);
    }

    /** @brief Clear newly live lanes residing in the former final padding. */
    void clear_previous_padding(size_type old_size, size_type new_size) noexcept
    {
        if (old_size == 0U) {
            return;
        }

        const auto lane = lane_index(old_size);
        if (lane == 0U) {
            return;
        }

        const auto newly_live_count = std::min(new_size - old_size, TileExtent - lane);
        clear_range(old_size, old_size + newly_live_count);
    }

    /** @brief Contiguous aligned allocation of complete physical tiles. */
    tile_container tiles_{};

    /** @brief Number of live records, excluding final-tile padding. */
    size_type logical_size_{};
};

/**
 * @brief Internal AoSoA backend for a valid schema and positive tile extent.
 *
 * Top-level schema cv-qualification is normalized away. The allocation-policy
 * parameter is an internal test seam; production tables use
 * @ref aligned_new_policy.
 *
 * @tparam Schema Valid schema, optionally top-level cv-qualified.
 * @tparam TileExtent Positive homogeneous physical tile extent.
 * @tparam AllocationPolicy Stateless raw allocation policy for the tile block.
 */
template<class Schema, std::size_t TileExtent, class AllocationPolicy = aligned_new_policy>
    requires valid_schema<Schema> && (TileExtent > 0U) && stateless_allocation_policy<AllocationPolicy>
using aosoa_storage = aosoa_storage_impl<std::remove_cv_t<Schema>, TileExtent, AllocationPolicy>;

} // namespace fieldpack::detail
