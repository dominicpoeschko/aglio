#pragma once

#include "type_descriptor.hpp"

#if __has_include(<fmt/format.h>)
    #include <fmt/chrono.h>
    #include <fmt/format.h>
    #include <fmt/ranges.h>
    #include <optional>
    #include <variant>

template<typename T>
struct fmt::formatter<std::optional<T>> : fmt::formatter<T> {
    template<typename FormatContext>
    constexpr auto format(std::optional<T> const& opt, FormatContext& ctx) const {
        if(opt) {
            formatter<T>::format(*opt, ctx);
            return ctx.out();
        }
        return format_to(ctx.out(), "()");
    }
};

template<typename... Ts>
struct fmt::formatter<std::variant<Ts...>> : fmt::dynamic_formatter<> {
    constexpr auto format(std::variant<Ts...> const& v, format_context& ctx) const {
        return std::visit(
          [&](auto const& val) { return fmt::dynamic_formatter<>::format(val, ctx); },
          v);
    }
};

template<aglio::Described T>
struct fmt::formatter<T> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    constexpr auto format(const T& v, FormatContext& ctx) -> decltype(ctx.out()) const {
        using td = aglio::TypeDescriptor<T>;

        constexpr std::size_t argCount = td::N_BaseClasses + td::N_Members;

        auto out = ctx.out();
        out      = format_to(out, "{}(", td::Name);

        std::size_t           n{};
        [[maybe_unused]] auto call = [&](auto const&... name_values) {
            [[maybe_unused]] auto sub_call = [&](auto const& name_value) {
                auto const& [name, value] = name_value;
                ++n;
                if(n == argCount) {
                    out = format_to(out, "\"{}\": {}", name, value);
                } else {
                    out = format_to(out, "\"{}\": {}, ", name, value);
                }
            };
            (sub_call(name_values), ...);
        };

        td::apply_named(call, v);

        return format_to(out, ")");
    }
};
#endif
