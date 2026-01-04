#pragma once

#include "type_descriptor.hpp"

#include <format>
#include <glaze/util/for_each.hpp>
#include <string>
#include <type_traits>

#ifdef AGLIO_FORMAT_DEFINE_STD
    #include <array>
    #include <map>
    #include <optional>
    #include <ranges>
    #include <set>
    #include <tuple>
    #include <utility>
    #include <variant>
    #include <vector>

template<aglio::Described T>
    requires(!std::ranges::range<T>)
struct std::formatter<T>;

namespace aglio::format::detail {
template<typename T>
concept IsStdString = std::is_same_v<std::decay_t<T>, std::string>;

template<typename T>
concept IsStdStringView = std::is_same_v<std::decay_t<T>, std::string_view>;

template<typename T>
concept IsCharPtr
  = std::is_same_v<std::decay_t<T>, char const*> || std::is_same_v<std::decay_t<T>, char*>;

template<typename T>
concept StringLike = IsStdString<T> || IsStdStringView<T> || IsCharPtr<T>;

template<typename OutputIt,
         typename T>
auto print_value(OutputIt out,
                 T const& value) {
    if constexpr(StringLike<T>) {
        return std::format_to(out, "\"{}\"", value);
    } else {
        return std::format_to(out, "{}", value);
    }
}
}   // namespace aglio::format::detail

// std::optional
template<typename T>
struct std::formatter<std::optional<T>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(std::optional<T> const& opt,
                FormatContext&          ctx) const {
        if(opt.has_value()) {
            auto out = std::format_to(ctx.out(), "optional(");
            out      = aglio::format::detail::print_value(out, *opt);
            return std::format_to(out, ")");
        } else {
            return std::format_to(ctx.out(), "none");
        }
    }
};

// std::variant
template<typename... Ts>
struct std::formatter<std::variant<Ts...>> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(std::variant<Ts...> const& v,
                FormatContext&             ctx) const {
        auto out = std::format_to(ctx.out(), "variant(");
        std::visit(
          [&out](auto const& value) { out = aglio::format::detail::print_value(out, value); },
          v);
        return std::format_to(out, ")");
    }
};

#endif

template<aglio::Described T>
    requires(!std::ranges::range<T>)
struct std::formatter<T> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(T const&       v,
                FormatContext& ctx) const {
        auto out   = ctx.out();
        out        = std::format_to(out, "{{");
        bool first = true;

        constexpr auto N = glz::reflect<T>::size;
        glz::for_each<N>([&]<auto I>() {
            if(!first) { out = std::format_to(out, ", "); }
            first = false;
            out   = std::format_to(out,
                                 "{}: {}",
                                 glz::reflect<T>::keys[I],
                                 glz::get<I>(glz::to_tie(v)));
        });

        return std::format_to(out, "}}");
    }
};
