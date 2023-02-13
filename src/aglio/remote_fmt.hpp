#pragma once

#include "type_descriptor.hpp"

#if __has_include("remote_fmt/remote_fmt.hpp")
    #include "remote_fmt/remote_fmt.hpp"

    #include <array>
    #include <cstddef>
    #include <string_view>
    #include <tuple>

template<aglio::Described T>
struct remote_fmt::formatter<T> {
    template<std::size_t Size>
    static consteval auto getNamedFmtString() {
        std::array<char, Size == 0 ? 4 : 2 + Size * 6> buff;
        auto                                           it  = buff.begin();
        auto                                           add = [&](std::string_view v) {
            for(auto c : v) {
                *it = c;
                ++it;
            }
        };
        add("{}(");

        for(std::size_t i = 0; i < Size - 1; ++i) {
            add("{:m}, ");
        }
        if(Size != 0) {
            add("{:m}");
        }

        add(")");
        return buff;
    }

    template<std::size_t Size>
    static constexpr auto named_fmt = getNamedFmtString<Size>();

    template<std::size_t Size>
    static constexpr auto named_fmt_sv
      = std::string_view{named_fmt<Size>.data(), named_fmt<Size>.size()};

    template<typename FormatContext>
    constexpr auto format(T const& v, FormatContext& ctx) const {
        using td = aglio::TypeDescriptor<T>;

        constexpr std::size_t argCount = td::N_BaseClasses + td::N_Members;

        [[maybe_unused]] auto call = [&](auto const&... name_values) {
            return format_to(
              ctx.out(),
              sc::create([]() { return named_fmt_sv<argCount>; }),
              td::Name,
              name_values...);
        };

        return std::apply(call, td::get_named_tuple(v));
    }
};
#endif
