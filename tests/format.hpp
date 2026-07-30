#pragma once
#include "check_format.hpp"
#include "types.hpp"

#include <format>

#define AGLIO_FORMAT_DEFINE_STD
#include <aglio/format.hpp>

namespace Test::format {
template<typename Type>
void test() {
    auto const t = Types::createDefault<Type>();
    auto const s = std::format("{}", t);

    Test::check_format<Type, Test::Api::Format>(s);
}
}   // namespace Test::format

TEMPLATE_LIST_TEST_CASE("format",
                        "[types]",
                        Types::List) {
    using Type = TestType;
    Test::format::test<Type>();
}
