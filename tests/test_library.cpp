#include <boost/ut.hpp>
#include <my_project/example.hpp>

int main()
{
    using namespace boost::ut;

    "the example library adds two integers"_test = [] { expect(my_project::add(20, 22) == 42_i); };
}
