#pragma once
#include "type_descriptor.hpp"

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace aglio {
template<typename Stream, typename T>
Stream& operator<<(Stream& os, std::vector<T> const& v) {
    bool first = true;
    os << '[';
    for(auto const& vv : v) {
        if(!first) {
            os << ", ";
        }
        first = false;
        os << vv;
    }
    os << ']';
    return os;
}

template<typename Stream, typename T>
Stream& operator<<(Stream& os, std::optional<T> const& v) {
    if(v) {
        os << *v;
    } else {
        os << "{}";
    }
    return os;
}

template<typename Stream, typename T, std::size_t N>
Stream& operator<<(Stream& os, std::array<T, N> const& v) {
    bool first = true;
    os << '[';
    for(auto const& vv : v) {
        if(!first) {
            os << ", ";
        }
        first = false;
        os << vv;
    }
    os << ']';
    return os;
}

template<typename Stream, typename T1, typename T2>
Stream& operator<<(Stream& os, std::pair<T1, T2> const& v) {
    os << '{' << v.first << ", " << v.second << '}';
    return os;
}

template<std::size_t... Ns, typename F>
auto tuple_loop(std::index_sequence<Ns...>, F f) {
    (f(std::integral_constant<std::size_t, Ns>{}), ...);
}

template<typename Stream, typename... Ts>
Stream& operator<<(Stream& os, std::tuple<Ts...> const& v) {
    bool first = true;
    os << '{';
    auto action = [&](auto i) {
        if(!first) {
            os << ", ";
        }
        first                          = false;
        static constexpr std::size_t I = decltype(i)::value;
        os << std::get<I>(v);
    };

    tuple_loop(std::make_index_sequence<sizeof...(Ts)>{}, action);
    os << '}';
    return os;
}

template<typename Stream, typename... Ts>
Stream& operator<<(Stream& os, std::variant<Ts...> const& v) {
    std::visit([&](auto const& vv) { os << vv; }, v);
    return os;
}

template<typename Stream, typename K, typename V>
Stream& operator<<(Stream& os, std::map<K, V> const& v) {
    bool first = true;
    os << '[';
    for(auto const& [kk, vv] : v) {
        if(!first) {
            os << ", ";
        }
        first = false;
        os << kk << ": " << vv;
    }
    os << ']';
    return os;
}

template<typename Stream, typename Rep, typename Period>
Stream& operator<<(Stream& os, std::chrono::duration<Rep, Period> const& v) {
    os << v.count();
    return os;
}

}   // namespace aglio

template<typename Stream, aglio::Described T>
Stream& operator<<(Stream& os, T const& v) {
    using td = aglio::TypeDescriptor<T>;
    os << '(';
    bool first = true;
    auto call  = [&](auto const&... values_names) {
        auto sub_call = [&](auto const& value_name) {
            auto const& [value, name] = value_name;
            if(!first) {
                os << ", ";

            } else {
                first = false;
            }
            os << name;
            os << ": ";
            using namespace aglio;
            using namespace std;
#if defined(__clang__)
            os << value;
#elif defined(__GNUC__) || defined(__GNUG__)
            if constexpr(requires { os << value; }) {
                os << value;
            } else if constexpr(requires { operator<<(os, value); }) {
                operator<<(os, value);
            }
#endif
        };
        (sub_call(values_names), ...);
    };

    td::base_class_apply_named(call, v);
    td::member_apply_named(call, v);
    os << ')';
    return os;
}
