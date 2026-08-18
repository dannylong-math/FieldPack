#include <boost/ut.hpp>
#include <fieldpack/example.hpp>

int main()
{
    using namespace boost::ut;

    "the example library adds two integers"_test = [] { expect(fieldpack::add(20, 22) == 42_i); };
}
