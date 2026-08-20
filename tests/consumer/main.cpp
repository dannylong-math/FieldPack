#include <cstddef>

// The consumer deliberately verifies the installed umbrella header.
// NOLINTBEGIN(misc-include-cleaner)
#include <fieldpack/fieldpack.hpp>

namespace {

struct position {};
struct velocity {};

using schema = fieldpack::schema<fieldpack::field<position, double>, fieldpack::field<velocity, double>>;

} // namespace

int main() // NOLINT(bugprone-exception-escape) -- checked consumer access reports invalid input
{
    fieldpack::table<schema, fieldpack::aosoa<16U>> values(17U);
    for (std::size_t index = 0U; index < values.size(); ++index) {
        values.at(index).get<position>() = static_cast<double>(index);
        values.at(index).get<velocity>() = 0.5;
    }

    using access = fieldpack::field_access<fieldpack::mutate<position>, fieldpack::read<velocity>>;
    fieldpack::for_each_chunk<8U>(values, access{}, [](auto chunk) {
        auto positions = chunk.template get<position>();
        const auto velocities = chunk.template get<velocity>();
        // Both spans have the callback-provided chunk size.
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        for (std::size_t lane = 0U; lane < chunk.size(); ++lane) {
            positions[lane] += velocities[lane];
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    });

    return values.at(16U).get<position>() == 16.5 ? 0 : 1;
}

// NOLINTEND(misc-include-cleaner)
