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
    constexpr auto format(T const& v, FormatContext& ctx) const -> decltype(ctx.out()) {
        using td = aglio::TypeDescriptor<T>;

        constexpr std::size_t argCount = td::N_BaseClasses + td::N_Members;

        auto out = ctx.out();
        out      = format_to(out, "{}(", td::Name);

        std::size_t           n{};
        [[maybe_unused]] auto call = [&](auto const&... name_values) {
            [[maybe_unused]] auto sub_call = [&](auto const& name_value) {
                auto const& [name, value] = name_value;
                ++n;
                std::string_view sep{", "};
                if(n == argCount) {
                    sep = "";
                }
                out = fmt::format_to(out, "\"{}\": {}{}", name, value, sep);
            };
            (sub_call(name_values), ...);
        };

        td::apply_named(call, v);

        return format_to(out, ")");
    }
};
#endif
