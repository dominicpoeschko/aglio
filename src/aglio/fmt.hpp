#pragma once

#include "type_descriptor.hpp"

#if __has_include(<fmt/format.h>)

    #ifdef __clang__
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wmacro-redefined"
        #pragma clang diagnostic ignored "-Wduplicate-enum"
        #pragma clang diagnostic ignored "-Wswitch"
        #pragma clang diagnostic ignored "-Wswitch-enum"
        #pragma clang diagnostic ignored "-Wundefined-func-template"
        #pragma clang diagnostic ignored "-Wfloat-equal"
        #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
        #pragma clang diagnostic ignored "-Wreserved-macro-identifier"
    #endif
    #include <fmt/chrono.h>
    #include <fmt/format.h>
    #include <fmt/ranges.h>
    #include <fmt/std.h>
    #ifdef __clang__
        #pragma clang diagnostic pop
    #endif

    // Supplies the enum formatter fmt lacks. Macro-gated, not __has_include: a missing header has to
    // fail loudly rather than silently drop enum support.
    #ifdef AGLIO_USE_ENCHANTUM
        #include <cstddef>
        #include <enchantum/fmt_format.hpp>

// std::byte is an enum, so BOTH fmt/std.h's `formatter<std::byte, Char>` and enchantum's
// `formatter<E> requires Enum<E>` match it and neither is more specialized - formatting anything
// containing a std::byte is a hard "ambiguous template instantiation" error. This full
// specialization is more specialized than either, and defers to fmt's, which prints the numeric
// value: enchantum would look for enumerator NAMES in a type that has none.
template<>
struct fmt::formatter<std::byte, char> : fmt::formatter<unsigned, char> {
    template<typename FormatContext>
    auto format(std::byte      b,
                FormatContext& ctx) const -> decltype(ctx.out()) {
        return fmt::formatter<unsigned, char>::format(static_cast<unsigned>(b), ctx);
    }
};
    #endif

template<aglio::Described T>
struct fmt::formatter<T> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(T const&       v,
                FormatContext& ctx) const -> decltype(ctx.out()) {
        auto out   = ctx.out();
        out        = fmt::format_to(out, "{{");
        bool first = true;

        constexpr auto N = glz::reflect<T>::size;
        glz::for_each<N>([&]<auto I>() {
            if(!first) { out = fmt::format_to(out, ", "); }
            first = false;
            out   = fmt::format_to(out,
                                   "{}: {}",
                                   glz::reflect<T>::keys[I],
                                   glz::get<I>(glz::to_tie(v)));
        });

        return fmt::format_to(out, "}}");
    }
};

#endif
