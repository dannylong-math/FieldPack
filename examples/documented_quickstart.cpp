#include <cstddef>
#include <cstdint>

// The umbrella header is the recommended user-facing include.
// NOLINTBEGIN(misc-include-cleaner)
#include <fieldpack/fieldpack.hpp>

namespace {

struct x {};
struct velocity_x {};
struct id {};

using particle_schema = fieldpack::schema<fieldpack::field<x, float>, fieldpack::field<velocity_x, float>,
                                          fieldpack::field<id, std::uint32_t>>;

using soa_particles = fieldpack::table<particle_schema, fieldpack::soa>;
using aosoa_particles = fieldpack::table<particle_schema, fieldpack::aosoa<64>>;

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- checked construction/access report errors to callers
{
    // This size produces eight full chunks of eight and one tail of three.
    aosoa_particles particles(67);

    auto first = particles.at(0);
    first.get<x>() = 2.0F;
    first.get<velocity_x>() = 0.5F;
    first.get<id>() = 0U;

    using drift_fields = fieldpack::field_access<fieldpack::mutate<x>, fieldpack::read<velocity_x>>;

    fieldpack::for_each_chunk<8>(particles, drift_fields{}, [](auto chunk) {
        auto positions = chunk.template get<x>();
        const auto velocities = chunk.template get<velocity_x>();

        // Both spans have the callback-provided chunk size.
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        for (std::size_t lane = 0; lane < chunk.size(); ++lane) {
            positions[lane] += velocities[lane];
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    });
}

// NOLINTEND(misc-include-cleaner)
