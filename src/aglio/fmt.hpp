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

template<aglio::Described T>
    requires(!fmt::is_range<T, char>::value)
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
