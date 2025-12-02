#pragma once

#include "type_descriptor.hpp"

#if __has_include(<fmt/format.h>)
    #include <fmt/chrono.h>
    #include <fmt/format.h>
    #include <fmt/ranges.h>
    #include <fmt/std.h>

template<aglio::Described T>
struct fmt::formatter<T> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template<typename FormatContext>
    constexpr auto format(T const&       v,
                          FormatContext& ctx) const -> decltype(ctx.out()) {
        constexpr auto N = glz::reflect<T>::size;

        auto out = ctx.out();
        out      = fmt::format_to(out, "{}(", glz::type_name<T>);

        auto const  tie = glz::to_tie(v);
        std::size_t n{};
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            auto process = [&]<std::size_t I>() {
                constexpr auto name  = glz::reflect<T>::keys[I];
                auto const&    value = get<I>(tie);
                ++n;
                std::string_view sep{", "};
                if(n == N) { sep = ""; }
                out = fmt::format_to(out, "\"{}\": {}{}", name, value, sep);
            };
            (process.template operator()<Is>(), ...);
        }(std::make_index_sequence<N>{});

        return fmt::format_to(out, ")");
    }
};
#endif
