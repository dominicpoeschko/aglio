#pragma once
#include "check_format.hpp"
#include "types.hpp"

#include <format>

template<typename T>
    requires std::is_enum_v<T>
struct std::formatter<T> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(T const&             v,
                std::format_context& ctx) const {
        using underlying = std::underlying_type_t<T>;
        return std::format_to(ctx.out(), "{}", static_cast<underlying>(v));
    }
};

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
