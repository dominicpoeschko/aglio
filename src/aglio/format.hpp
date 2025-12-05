#pragma once

#include "type_descriptor.hpp"

#include <format>
#include <glaze/util/for_each.hpp>
#include <string>
#include <type_traits>   // Required for std::is_same_v, std::decay_t

#ifdef AGLIO_FORMAT_DEFINE_STD
    #include <array>
    #include <map>
    #include <optional>
    #include <set>
    #include <tuple>
    #include <utility>
    #include <variant>
    #include <vector>
#endif

template<aglio::Described T>
struct std::formatter<T> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(T const&             v,
                std::format_context& ctx) const {
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

#ifdef AGLIO_FORMAT_DEFINE_STD

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

// std::pair
template<typename T1, typename T2>
struct std::formatter<std::pair<T1, T2>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::pair<T1,
                          T2> const& p,
                std::format_context& ctx) const {
        auto out = std::format_to(ctx.out(), "(");
        out      = aglio::format::detail::print_value(out, p.first);
        out      = std::format_to(out, ", ");
        out      = aglio::format::detail::print_value(out, p.second);
        return std::format_to(out, ")");
    }
};

// std::tuple
template<typename... Ts>
struct std::formatter<std::tuple<Ts...>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::tuple<Ts...> const& t,
                std::format_context&     ctx) const {
        auto out   = std::format_to(ctx.out(), "(");
        bool first = true;
        std::apply(
          [&](auto const&... values) {
              auto print_one = [&](auto const& val) {
                  if(!first) { out = std::format_to(out, ", "); }
                  first = false;
                  out   = aglio::format::detail::print_value(out, val);
              };
              (print_one(values), ...);
          },
          t);
        return std::format_to(out, ")");
    }
};

// std::optional
template<typename T>
struct std::formatter<std::optional<T>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::optional<T> const& opt,
                std::format_context&    ctx) const {
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
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::variant<Ts...> const& v,
                std::format_context&       ctx) const {
        auto out = std::format_to(ctx.out(), "variant(");
        std::visit(
          [&out](auto const& value) { out = aglio::format::detail::print_value(out, value); },
          v);
        return std::format_to(out, ")");
    }
};

// std::vector
template<typename T, typename... Args>
struct std::formatter<std::vector<T, Args...>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::vector<T,
                            Args...> const& v,
                std::format_context&        ctx) const {
        auto out   = std::format_to(ctx.out(), "[");
        bool first = true;
        for(auto const& elem : v) {
            if(!first) { out = std::format_to(out, ", "); }
            first = false;
            out   = aglio::format::detail::print_value(out, elem);
        }
        return std::format_to(out, "]");
    }
};

// std::array
template<typename T, std::size_t N>
struct std::formatter<std::array<T, N>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::array<T,
                           N> const& a,
                std::format_context& ctx) const {
        auto out   = std::format_to(ctx.out(), "[");
        bool first = true;
        for(auto const& elem : a) {
            if(!first) { out = std::format_to(out, ", "); }
            first = false;
            out   = aglio::format::detail::print_value(out, elem);
        }
        return std::format_to(out, "]");
    }
};

// std::set
template<typename T, typename... Args>
struct std::formatter<std::set<T, Args...>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::set<T,
                         Args...> const& s,
                std::format_context&     ctx) const {
        auto out   = std::format_to(ctx.out(), "{{");
        bool first = true;
        for(auto const& elem : s) {
            if(!first) { out = std::format_to(out, ", "); }
            first = false;
            out   = aglio::format::detail::print_value(out, elem);
        }
        return std::format_to(out, "}}");
    }
};

// std::map
template<typename K, typename V, typename... Args>
struct std::formatter<std::map<K, V, Args...>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(std::map<K,
                         V,
                         Args...> const& m,
                std::format_context&     ctx) const {
        auto out   = std::format_to(ctx.out(), "{{");
        bool first = true;
        for(auto const& [key, value] : m) {
            if(!first) { out = std::format_to(out, ", "); }
            first = false;
            out   = aglio::format::detail::print_value(out, key);
            out   = std::format_to(out, ": ");
            out   = aglio::format::detail::print_value(out, value);
        }
        return std::format_to(out, "}}");
    }
};

#endif   // AGLIO_FORMAT_DEFINE_STD
