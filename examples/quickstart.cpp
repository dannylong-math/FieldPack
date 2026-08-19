#include <cstddef>
#include <cstdint>

// This user-facing example intentionally verifies and demonstrates the public
// umbrella header instead of listing the narrower declaring headers.
// NOLINTBEGIN(misc-include-cleaner)
#include <fieldpack/fieldpack.hpp>

namespace {

struct x {};
struct velocity_x {};
struct id {};

using particle_schema = fieldpack::schema<fieldpack::field<x, float>, fieldpack::field<velocity_x, float>,
                                          fieldpack::field<id, std::uint32_t>>;

template<class Layout> using particle_table = fieldpack::table<particle_schema, Layout>;

template<class Layout> void initialize(particle_table<Layout>& particles)
{
    for (std::size_t index = 0U; index < particles.size(); ++index) {
        auto particle = particles.at(index);
        particle.template get<x>() = static_cast<float>(index);
        particle.template get<velocity_x>() = 0.5F;
        particle.template get<id>() = static_cast<std::uint32_t>(index);
    }
}

template<class Layout> void drift(particle_table<Layout>& particles, float time_step)
{
    using fields = fieldpack::field_access<fieldpack::mutate<x>, fieldpack::read<velocity_x>>;

    fieldpack::for_each_chunk<8U>(particles, fields{}, [time_step](auto chunk) {
        auto positions = chunk.template get<x>();
        const auto velocities = chunk.template get<velocity_x>();

        // Each span subscript is bounded by the callback-provided chunk size.
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        for (std::size_t lane = 0U; lane < chunk.size(); ++lane) {
            positions[lane] += time_step * velocities[lane];
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    });
}

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- checked example access reports invalid indices to the caller
{
    // 67 records produce eight full chunks of eight and one tail of three.
    particle_table<fieldpack::soa> soa_particles(67U);
    particle_table<fieldpack::aosoa<64U>> aosoa_particles(67U);

    initialize(soa_particles);
    initialize(aosoa_particles);

    // Scalar row access is independent of the selected storage layout.
    soa_particles.at(0U).get<x>() = 2.0F;
    aosoa_particles.at(0U).get<x>() = 2.0F;

    drift(soa_particles, 2.0F);
    drift(aosoa_particles, 2.0F);

    const auto soa_last = static_cast<const decltype(soa_particles)&>(soa_particles).at(66U);
    const auto aosoa_last = static_cast<const decltype(aosoa_particles)&>(aosoa_particles).at(66U);
    return soa_last.get<x>() == 67.0F && aosoa_last.get<x>() == 67.0F ? 0 : 1;
}

// NOLINTEND(misc-include-cleaner)
