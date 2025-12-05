#pragma once
#include "check_format.hpp"
#include "types.hpp"

#include <aglio/fmt.hpp>

template<typename T>
    requires std::is_enum_v<T>
struct fmt::formatter<T> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(T const&       v,
                FormatContext& ctx) const {
        using underlying = std::underlying_type_t<T>;
        return fmt::format_to(ctx.out(), "{}", static_cast<underlying>(v));
    }
};

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
