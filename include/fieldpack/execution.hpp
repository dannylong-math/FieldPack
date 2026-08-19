#pragma once // NOLINT(portability-avoid-pragma-once) -- project-wide header convention

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <fieldpack/layout.hpp>
#include <fieldpack/schema.hpp>
#include <fieldpack/table.hpp>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>

/**
 * @file execution.hpp
 * @brief Named fixed-chunk traversal with centralized logical tails.
 */

namespace fieldpack {

/**
 * @brief Request read-only chunk access to one named schema field.
 *
 * A read descriptor always exposes `std::span<const T, Extent>`, including
 * when traversal receives a mutable table.
 *
 * @tparam Tag Exact schema tag requested by a kernel.
 */
template<class Tag> struct read {
    /** @brief Exact schema tag requested for read-only access. */
    using tag = Tag;
};

/**
 * @brief Request mutable chunk access to one named schema field.
 *
 * A mutation descriptor exposes `std::span<T, Extent>` and therefore cannot
 * be used when traversing a const table.
 *
 * @tparam Tag Exact schema tag requested by a kernel.
 */
template<class Tag> struct mutate {
    /** @brief Exact schema tag requested for mutable access. */
    using tag = Tag;
};

/**
 * @brief Internal access validation and chunk-execution machinery.
 */
namespace detail {

/**
 * @brief Recognize types that are not access descriptors.
 *
 * The primary template deliberately has no `tag` alias, allowing validation
 * to reject unrelated types before tag metadata is instantiated.
 *
 * @tparam Candidate Type inspected as an access descriptor.
 */
template<class Candidate> struct access_traits {
    /** @brief Whether @p Candidate is an exact supported descriptor. */
    static constexpr bool is_access = false;
};

/** @brief Extract metadata from a read-only access descriptor. */
template<class Tag> struct access_traits<read<Tag>> {
    /** @brief Marks this specialization as a supported descriptor. */
    static constexpr bool is_access = true;

    /** @brief Read descriptors do not grant mutation. */
    static constexpr bool is_mutating = false;

    /** @brief Exact schema tag requested by the descriptor. */
    using tag = Tag;
};

/** @brief Extract metadata from a mutable access descriptor. */
template<class Tag> struct access_traits<mutate<Tag>> {
    /** @brief Marks this specialization as a supported descriptor. */
    static constexpr bool is_access = true;

    /** @brief Mutation descriptors grant writable access. */
    static constexpr bool is_mutating = true;

    /** @brief Exact schema tag requested by the descriptor. */
    using tag = Tag;
};

/**
 * @brief Validate a non-empty list of exact, uniquely tagged descriptors.
 *
 * @tparam Access Candidate access descriptors.
 * @return `true` exactly when the list can form @ref field_access.
 */
template<class... Access> consteval auto valid_access_list() noexcept -> bool
{
    if constexpr (sizeof...(Access) == 0U || !(access_traits<Access>::is_access && ...)) {
        return false;
    }
    else {
        return unique_types<typename access_traits<Access>::tag...>::value;
    }
}

} // namespace detail

/**
 * @brief Describe the named fields and permissions requested by a kernel.
 *
 * The list must be non-empty, contain only exact @ref read and @ref mutate
 * specializations, and name each tag at most once. Whether those tags occur
 * in a particular schema is checked when @ref for_each_chunk is called.
 *
 * @tparam Access Unique read or mutate descriptors.
 *
 * @code{.cpp}
 * using drift_fields = fieldpack::field_access<
 *     fieldpack::mutate<x>,
 *     fieldpack::read<velocity_x>>;
 * @endcode
 */
template<class... Access>
    requires(detail::valid_access_list<Access...>())
struct field_access {};

namespace detail {

/** @brief Report whether an access pack contains one exact tag. */
template<class Tag, class... Access>
inline constexpr bool access_list_contains_tag_v =
    // `typename` is required by both supported compilers despite the tidy
    // check treating the dependent pack member as non-dependent.
    // NOLINTNEXTLINE(readability-redundant-typename)
    (std::same_as<Tag, typename access_traits<Access>::tag> || ...);

/**
 * @brief Dispatch access selection after comparing the first descriptor tag.
 *
 * @tparam Tag Exact tag being selected.
 * @tparam Matches Whether the first descriptor names @p Tag.
 * @tparam First First descriptor under consideration.
 * @tparam Rest Remaining access descriptors.
 */
template<class Tag, bool Matches, class First, class... Rest> struct access_for_tag_step;

/** @brief Stop access selection at the matching descriptor. */
template<class Tag, class First, class... Rest> struct access_for_tag_step<Tag, true, First, Rest...> {
    /** @brief Descriptor that names @p Tag. */
    using type = First;
};

/** @brief Continue access selection after a non-matching descriptor. */
template<class Tag, class First, class... Rest> struct access_for_tag_step<Tag, false, First, Rest...>;

/**
 * @brief Recursively select an access descriptor by its exact tag.
 *
 * No empty-pack case is provided because callers constrain lookup to tags
 * already present in the access pack.
 */
template<class Tag, class First, class... Rest>
struct access_for_tag
    : access_for_tag_step<Tag, std::same_as<Tag, typename access_traits<First>::tag>, First, Rest...> {};

template<class Tag, class First, class... Rest>
struct access_for_tag_step<Tag, false, First, Rest...> : access_for_tag<Tag, Rest...> {};

/** @brief Access descriptor selected from a uniquely tagged pack. */
template<class Tag, class... Access> using access_for_tag_t = access_for_tag<Tag, Access...>::type;

/** @brief Value type stored for one accessed schema tag. */
template<class Schema, class Access> using access_value_t = field_type_t<Schema, typename access_traits<Access>::tag>;

/**
 * @brief Span type exposed for one descriptor and chunk extent.
 *
 * Read descriptors add constness to the element type; mutation descriptors
 * preserve the mutable schema value type.
 */
template<class Schema, std::size_t Extent, class Access>
using access_span_t = std::span<std::conditional_t<access_traits<Access>::is_mutating, access_value_t<Schema, Access>,
                                                   const access_value_t<Schema, Access>>,
                                Extent>;

/**
 * @brief Store the span associated with one access descriptor.
 *
 * @tparam Schema Unqualified logical schema.
 * @tparam Extent Fixed chunk extent or `std::dynamic_extent` for the tail.
 * @tparam Access Read or mutation descriptor naming the span.
 */
template<class Schema, std::size_t Extent, class Access> class chunk_field_view {
public:
    /** @brief Permission-correct span type held for this field. */
    using span_type = access_span_t<Schema, Extent, Access>;

    /**
     * @brief Store a non-owning field span.
     *
     * @param values Contiguous live values for one named field.
     */
    explicit constexpr chunk_field_view(span_type values) noexcept : values_(values) {}

    /** @brief Return a copy of the stored non-owning span. */
    [[nodiscard]] constexpr auto span() const noexcept -> span_type { return values_; }

private:
    /** @brief Non-owning named field range. */
    span_type values_;
};

/**
 * @brief Bundle permission-correct named spans for one logical chunk.
 *
 * A const bundle retains the permissions encoded by its descriptors, just as
 * a const `std::span<T>` still refers to mutable `T`. The bundle and returned
 * spans are invalidated whenever their source table storage is invalidated.
 *
 * @tparam Schema Unqualified logical schema.
 * @tparam Extent Fixed chunk extent or `std::dynamic_extent` for the tail.
 * @tparam Access Unique access descriptors included in this bundle.
 */
template<class Schema, std::size_t Extent, class... Access>
// One base per uniquely tagged access stores heterogeneous spans without
// positional lookup in the callback-facing API.
// NOLINTNEXTLINE(misc-multiple-inheritance)
class named_chunk_view : private chunk_field_view<Schema, Extent, Access>... {
private:
    /** @brief Field-view base associated with one access descriptor. */
    template<class SelectedAccess> using field_view = chunk_field_view<Schema, Extent, SelectedAccess>;

public:
    /** @brief Unsigned type used for the number of live chunk elements. */
    using size_type = std::size_t;

    /** @brief Compile-time extent of each span in this bundle. */
    static constexpr size_type extent = Extent;

    /**
     * @brief Assemble one named bundle from equally sized field spans.
     *
     * @param count Number of live logical elements represented by every span.
     * @param spans Permission-correct spans in descriptor declaration order.
     */
    constexpr named_chunk_view(size_type count, access_span_t<Schema, Extent, Access>... spans) noexcept :
        field_view<Access>(spans)..., size_(count)
    {
    }

    /**
     * @brief Retrieve a requested field span by exact tag.
     *
     * @tparam Tag Tag present in this bundle's access list.
     * @return Copy of the non-owning permission-correct field span.
     */
    template<class Tag>
        requires access_list_contains_tag_v<Tag, Access...>
    [[nodiscard]] constexpr decltype(auto) get() const noexcept
    {
        using selected_access = access_for_tag_t<Tag, Access...>;
        return static_cast<const field_view<selected_access>&>(*this).span();
    }

    /**
     * @brief Return the number of live logical elements in this chunk.
     *
     * Fixed bundles report their compile-time extent directly, keeping the
     * full-chunk loop bound visible to optimizers. Dynamic tail bundles use
     * the runtime live count supplied during construction.
     */
    [[nodiscard]] constexpr auto size() const noexcept -> size_type
    {
        if constexpr (Extent == std::dynamic_extent) {
            return size_;
        }
        else {
            return Extent;
        }
    }

    /** @brief Determine whether this chunk contains no live elements. */
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size() == 0U; }

private:
    /** @brief Shared live length of every named field span. */
    size_type size_;
};

/**
 * @brief Obtain private backend storage from an owning table.
 *
 * Only the execution layer uses this friend bridge. It does not expose raw
 * storage through the public table interface.
 */
struct table_access {
    /** @brief Return mutable storage from a mutable table. */
    template<class Schema, class Layout>
    [[nodiscard]] static constexpr decltype(auto) storage(table<Schema, Layout>& values) noexcept
    {
        return (values.storage_);
    }

    /** @brief Return immutable storage from a const table. */
    template<class Schema, class Layout>
    [[nodiscard]] static constexpr decltype(auto) storage(const table<Schema, Layout>& values) noexcept
    {
        return (values.storage_);
    }
};

/** @brief Unqualified schema represented by a storage backend. */
template<class Storage> using storage_schema_t = std::remove_cvref_t<Storage>::schema_type;

/** @brief Named bundle type produced for a backend and access list. */
template<class Storage, std::size_t Extent, class... Access>
using chunk_view_t = named_chunk_view<storage_schema_t<Storage>, Extent, Access...>;

/** @brief Report whether every requested tag occurs in a schema. */
template<class Schema, class... Access>
inline constexpr bool accesses_known_v =
    // `typename` is required by both supported compilers despite the tidy
    // check treating the dependent pack member as non-dependent.
    // NOLINTNEXTLINE(readability-redundant-typename)
    (contains_tag_v<Schema, typename access_traits<Access>::tag> && ...);

/** @brief Report whether an access list requests any mutation. */
template<class... Access> inline constexpr bool has_mutation_v = (access_traits<Access>::is_mutating || ...);

/**
 * @brief Check whether a chunk extent is compatible with a layout.
 *
 * SoA accepts every positive extent. AoSoA additionally requires the chunk
 * extent to divide its physical tile extent, ensuring full chunks never cross
 * tile boundaries and leaving at most one final logical tail.
 */
template<std::size_t ChunkExtent, class Layout> consteval auto compatible_chunk_extent() noexcept -> bool
{
    if constexpr (ChunkExtent == 0U) {
        return false;
    }
    else if constexpr (layout_traits<Layout>::is_tiled) {
        return layout_traits<Layout>::tile_extent % ChunkExtent == 0U;
    }
    else {
        return true;
    }
}

/** @brief Determine whether a callback accepts both full and tail bundles. */
template<class Function, class Storage, std::size_t ChunkExtent, class... Access>
concept chunk_callback = std::invocable<Function&, chunk_view_t<Storage, ChunkExtent, Access...>> &&
                         std::invocable<Function&, chunk_view_t<Storage, std::dynamic_extent, Access...>>;

/** @brief Determine whether both callback bundle invocations are non-throwing. */
template<class Function, class Storage, std::size_t ChunkExtent, class... Access>
inline constexpr bool nothrow_chunk_callback_v =
    std::is_nothrow_invocable_v<Function&, chunk_view_t<Storage, ChunkExtent, Access...>> &&
    std::is_nothrow_invocable_v<Function&, chunk_view_t<Storage, std::dynamic_extent, Access...>>;

/**
 * @brief Construct one descriptor's permission-correct backend span.
 */
template<std::size_t Extent, class Access, class Storage>
[[nodiscard]] constexpr auto make_access_span(Storage& storage, std::size_t first, std::size_t count) noexcept
    -> access_span_t<storage_schema_t<Storage>, Extent, Access>
{
    using tag = access_traits<Access>::tag;
    if constexpr (access_traits<Access>::is_mutating) {
        return storage.template contiguous_span<tag, Extent>(first, count);
    }
    else {
        return std::as_const(storage).template contiguous_span<tag, Extent>(first, count);
    }
}

/**
 * @brief Construct a named bundle over one validated contiguous range.
 */
template<std::size_t Extent, class... Access, class Storage>
[[nodiscard]] constexpr auto make_chunk_view(Storage& storage, std::size_t first,
                                             std::size_t count) noexcept -> chunk_view_t<Storage, Extent, Access...>
{
    return {count, make_access_span<Extent, Access>(storage, first, count)...};
}

/**
 * @brief Invoke a callback with one fixed-extent named chunk bundle.
 */
template<std::size_t ChunkExtent, class... Access, class Storage, class Function>
constexpr void invoke_full_chunk(Storage& storage, std::size_t first, std::size_t count, Function& function) noexcept(
    std::is_nothrow_invocable_v<Function&, chunk_view_t<Storage, ChunkExtent, Access...>>)
{
    std::invoke(function, make_chunk_view<ChunkExtent, Access...>(storage, first, count));
}

/**
 * @brief Invoke a callback with the sole dynamic-extent logical tail.
 *
 * Centralizing this dispatch keeps cleanup policy out of domain kernels and
 * gives future backends one location in which to substitute masked tails.
 */
template<class... Access, class Storage, class Function>
constexpr void invoke_tail(Storage& storage, std::size_t first, std::size_t count, Function& function) noexcept(
    std::is_nothrow_invocable_v<Function&, chunk_view_t<Storage, std::dynamic_extent, Access...>>)
{
    std::invoke(function, make_chunk_view<std::dynamic_extent, Access...>(storage, first, count));
}

/**
 * @brief Execute all fixed chunks followed by at most one dynamic tail.
 */
template<std::size_t ChunkExtent, class... Access, class Storage, class Function>
    requires chunk_callback<Function, Storage, ChunkExtent, Access...>
constexpr void
execute_chunks(Storage& storage,
               Function& function) noexcept(nothrow_chunk_callback_v<Function, Storage, ChunkExtent, Access...>)
{
    const auto logical_size = storage.size();
    const auto full_chunk_count = logical_size / ChunkExtent;
    std::size_t first = 0U;

    // Exhaustive tests cover zero, one, and many iterations. Remaining arcs
    // are compiler-generated cleanup edges for arbitrary callback exceptions.
    for (std::size_t chunk = 0U; chunk < full_chunk_count; ++chunk) { // GCOVR_EXCL_BR_LINE
        const auto count = std::min(ChunkExtent, storage.contiguous_count(first));
        invoke_full_chunk<ChunkExtent, Access...>(storage, first, count, function);
        first += ChunkExtent;
    }

    // Extent one divides every possible size, so it cannot instantiate or
    // execute a dynamic-tail helper. Larger extents retain one centralized
    // runtime tail check outside the full-chunk loop.
    if constexpr (ChunkExtent > 1U) {
        // Exhaustive tests cover both exact and tail sizes. Remaining arcs are
        // compiler-generated cleanup edges for arbitrary callback exceptions.
        if (first != logical_size) { // GCOVR_EXCL_BR_LINE
            const auto count = std::min(logical_size - first, storage.contiguous_count(first));
            invoke_tail<Access...>(storage, first, count, function);
        }
    }
}

} // namespace detail

/**
 * @brief Apply one named kernel to fixed chunks and an optional logical tail.
 *
 * Full callbacks receive spans with compile-time extent @p ChunkExtent. If the
 * table size is not divisible by that extent, one final callback receives
 * dynamic-extent spans containing only the remaining live elements. Read
 * descriptors produce const spans and mutation descriptors produce mutable
 * spans. Callback exceptions propagate without rolling back earlier changes.
 *
 * @tparam ChunkExtent Positive fixed extent used by the full-chunk loop.
 * @tparam Schema Valid logical schema stored by the table.
 * @tparam Layout Recognized layout compatible with @p ChunkExtent.
 * @tparam Access Unique known field-access descriptors.
 * @tparam Function Callback invocable with both fixed and dynamic bundles.
 * @param values Mutable table supplying named field storage.
 * @param access Stateless field-access description.
 * @param function Kernel invoked in increasing logical-index order.
 *
 * @code{.cpp}
 * using fields = fieldpack::field_access<
 *     fieldpack::mutate<x>,
 *     fieldpack::read<velocity_x>>;
 *
 * fieldpack::for_each_chunk<8>(particles, fields{}, [](auto chunk) {
 *     auto xs = chunk.template get<x>();
 *     auto velocities = chunk.template get<velocity_x>();
 *     for (std::size_t lane = 0; lane < chunk.size(); ++lane) {
 *         xs[lane] += velocities[lane];
 *     }
 * });
 * @endcode
 */
template<std::size_t ChunkExtent, class Schema, class Layout, class... Access, class Function>
    requires(detail::compatible_chunk_extent<ChunkExtent, Layout>() &&
             detail::accesses_known_v<std::remove_cv_t<Schema>, Access...> &&
             detail::chunk_callback<Function, detail::table_storage_t<Schema, Layout>, ChunkExtent, Access...>)
constexpr void
for_each_chunk(table<Schema, Layout>& values, [[maybe_unused]] field_access<Access...> access,
               Function&& function) // NOLINT(cppcoreguidelines-missing-std-forward) -- invoked repeatedly as an lvalue
    noexcept(
        detail::nothrow_chunk_callback_v<Function, detail::table_storage_t<Schema, Layout>, ChunkExtent, Access...>)
{
    auto& storage = detail::table_access::storage(values);
    detail::execute_chunks<ChunkExtent, Access...>(storage, function);
}

/**
 * @brief Apply one read-only named kernel to a const table.
 *
 * This overload has the same chunk and tail behavior as mutable traversal but
 * participates only when no descriptor requests mutation.
 *
 * @tparam ChunkExtent Positive fixed extent used by the full-chunk loop.
 * @tparam Schema Valid logical schema stored by the table.
 * @tparam Layout Recognized layout compatible with @p ChunkExtent.
 * @tparam Access Unique known read descriptors.
 * @tparam Function Callback invocable with both fixed and dynamic bundles.
 * @param values Const table supplying read-only named field storage.
 * @param access Stateless read-access description.
 * @param function Kernel invoked in increasing logical-index order.
 */
template<std::size_t ChunkExtent, class Schema, class Layout, class... Access, class Function>
    requires(detail::compatible_chunk_extent<ChunkExtent, Layout>() && !detail::has_mutation_v<Access...> &&
             detail::accesses_known_v<std::remove_cv_t<Schema>, Access...> &&
             detail::chunk_callback<Function, const detail::table_storage_t<Schema, Layout>, ChunkExtent, Access...>)
constexpr void
for_each_chunk(const table<Schema, Layout>& values, [[maybe_unused]] field_access<Access...> access,
               Function&& function) // NOLINT(cppcoreguidelines-missing-std-forward) -- invoked repeatedly as an lvalue
    noexcept(detail::nothrow_chunk_callback_v<Function, const detail::table_storage_t<Schema, Layout>, ChunkExtent,
                                              Access...>)
{
    const auto& storage = detail::table_access::storage(values);
    detail::execute_chunks<ChunkExtent, Access...>(storage, function);
}

} // namespace fieldpack
