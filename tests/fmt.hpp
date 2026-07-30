#pragma once
#include "check_format.hpp"
#include "types.hpp"

#include <aglio/fmt.hpp>

namespace Test::fmt {
template<typename Type>
void test() {
    auto const t = Types::createDefault<Type>();
    auto const s = ::fmt::format("{}", t);

    Test::check_format<Type, Test::Api::Fmt>(s);
}
}   // namespace Test::fmt

TEMPLATE_LIST_TEST_CASE("fmt",
                        "[types]",
                        Types::List) {
    using Type = TestType;
    Test::fmt::test<Type>();
}
