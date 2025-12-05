#pragma once
#include "check_format.hpp"
#include "types.hpp"

#define AGLIO_OSTREAM_DEFINE_STD
#include <aglio/ostream.hpp>
#include <sstream>

namespace Test::ostream {
template<typename Type>
void test() {
    auto const        t = Types::createDefault<Type>();
    std::stringstream ss;
    ss << t;

    auto const s = ss.str();

    Test::check_format<Type, Test::Api::Ostream>(s);
}
}   // namespace Test::ostream

TEMPLATE_LIST_TEST_CASE("ostream",
                        "[types]",
                        Types::List) {
    using Type = TestType;
    Test::ostream::test<Type>();
}
