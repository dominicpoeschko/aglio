#pragma once

#include "type_descriptor.hpp"

#if __has_include("remote_fmt/remote_fmt.hpp")
    #include "remote_fmt/remote_fmt.hpp"

    #include <array>
    #include <cstddef>
    #include <string_view>
    #include <tuple>
    #include <utility>

template<aglio::Described T>
struct remote_fmt::formatter<T> {
    static consteval auto getNamedFmtString() {
        constexpr auto  n    = glz::reflect<T>::size;
        constexpr auto& keys = glz::reflect<T>::keys;

        constexpr std::size_t size = [] {
            std::size_t s{};
            s += glz::type_name<T>.size();
            s += 11;   // "@TYPENAME(" and ")"
            s += 4;    // "{{" and "}}"
            if constexpr(n != 0) {
                s += (n - 1) * 2;   // ", "
                s += n * 4;         // ": {}"
                for(std::size_t i = 0; i < n; ++i) { s += keys[i].size(); }
            }
            return s;
        }();

        std::array<char, size> buff;
        auto                   it = buff.begin();
        auto add = [&](std::string_view v) { it = std::copy(v.begin(), v.end(), it); };

        add("@TYPENAME(");
        add(glz::type_name<T>);
        add(")");
        add("{{");

        if constexpr(n > 0) {
            for(std::size_t i = 0; i < n; ++i) {
                add(keys[i]);
                add(": {}");
                if(i < n - 1) { add(", "); }
            }
        }

        add("}}");
        return buff;
    }

    static constexpr auto namedFmt   = getNamedFmtString();
    static constexpr auto namedFmtSv = std::string_view{namedFmt.data(), namedFmt.size()};

    // The sub format string above with the fields' own descriptors (remote_fmt's `F`): static, so a type entry of
    // the catalog - only the field values go on the wire.
    static consteval auto text() {
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            return remote_fmt::subFormatDescriptor<glz::field_t<T, Is>...>(
              sc::create([] { return namedFmtSv; }));
        }(std::make_index_sequence<glz::reflect<T>::size>{});
    }

    static consteval auto descriptor() { return remote_fmt::typeDescriptor<decltype(text())>(); }

    template<typename Printer>
    constexpr void format(T const& v,
                          Printer& printer) const {
        glz::apply([&](auto const&... values) { printer.formatArguments(values...); },
                   glz::to_tie(v));
    }
};
#endif
