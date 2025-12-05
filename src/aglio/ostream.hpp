#pragma once
#include "type_descriptor.hpp"

#include <iostream>
#include <string_view>

#ifdef AGLIO_OSTREAM_DEFINE_STD
    #include <array>
    #include <map>
    #include <optional>
    #include <set>
    #include <tuple>
    #include <utility>
    #include <variant>
    #include <vector>

// Forward declarations for std library types
template<typename Stream,
         typename T1,
         typename T2>
Stream& operator<<(Stream&              os,
                   std::pair<T1,
                             T2> const& p);

template<typename Stream,
         typename... Ts>
Stream& operator<<(Stream&                  os,
                   std::tuple<Ts...> const& t);

template<typename Stream,
         typename T>
Stream& operator<<(Stream&                 os,
                   std::optional<T> const& opt);

template<typename Stream,
         typename... Ts>
Stream& operator<<(Stream&                    os,
                   std::variant<Ts...> const& v);

template<typename Stream,
         typename T,
         typename... Args>
Stream& operator<<(Stream&                     os,
                   std::vector<T,
                               Args...> const& v);

template<typename Stream,
         typename T,
         std::size_t N>
Stream& operator<<(Stream&              os,
                   std::array<T,
                              N> const& a);

template<typename Stream,
         typename T,
         typename... Args>
Stream& operator<<(Stream&                  os,
                   std::set<T,
                            Args...> const& s);

template<typename Stream,
         typename K,
         typename V,
         typename... Args>
Stream& operator<<(Stream&                  os,
                   std::map<K,
                            V,
                            Args...> const& m);

#endif

namespace aglio::detail {

// Helper to print a single key-value pair.
template<typename Stream,
         typename T>
void print_member(Stream&          os,
                  std::string_view name,
                  T const&         value);

// Helper to iterate over the members of a Described type.
template<typename Stream,
         aglio::Described T,
         std::size_t... Is>
void print_members(Stream&  os,
                   T const& v,
                   std::index_sequence<Is...>);
}   // namespace aglio::detail

template<typename Stream,
         aglio::Described T>
Stream& operator<<(Stream&  os,
                   T const& v) {
    os << '{';
    aglio::detail::print_members(os, v, std::make_index_sequence<glz::reflect<T>::size>{});
    os << '}';
    return os;
}

namespace aglio::detail {

// Helper to print a single key-value pair.
template<typename Stream,
         typename T>
void print_member(Stream&          os,
                  std::string_view name,
                  T const&         value) {
    os << name;
    os << ": ";
    if constexpr(requires { os << value; }) {
        os << value;
    } else {
        operator<<(os, value);
    }
}

// Helper to iterate over the members of a Described type.
template<typename Stream,
         aglio::Described T,
         std::size_t... Is>
void print_members(Stream&  os,
                   T const& v,
                   std::index_sequence<Is...>) {
    auto tie            = glz::to_tie(v);
    bool first          = true;
    auto process_member = [&](auto i) {
        constexpr std::size_t I = decltype(i)::value;
        if(!first) { os << ", "; }
        first = false;
        print_member(os, glz::reflect<T>::keys[I], glz::get<I>(tie));
    };
    (process_member(std::integral_constant<std::size_t, Is>{}), ...);
}
}   // namespace aglio::detail

#ifdef AGLIO_OSTREAM_DEFINE_STD
namespace aglio::ostream::detail {
template<typename T>
concept IsStdString = std::is_same_v<std::decay_t<T>, std::string>;

template<typename T>
concept IsStdStringView = std::is_same_v<std::decay_t<T>, std::string_view>;

template<typename T>
concept IsCharPtr
  = std::is_same_v<std::decay_t<T>, char const*> || std::is_same_v<std::decay_t<T>, char*>;

template<typename T>
concept StringLike = IsStdString<T> || IsStdStringView<T> || IsCharPtr<T>;

template<typename Stream,
         typename T>
Stream& print_value(Stream&  os,
                    T const& value) {
    if constexpr(StringLike<T>) {
        os << "\"";
        os << value;
        os << "\"";
        return os;
    } else {
        os << value;
        return os;
    }
}
}   // namespace aglio::ostream::detail

namespace aglio::ostream::detail {
template<typename Stream,
         typename Tuple,
         std::size_t... Is>
Stream& print_tuple(Stream&      os,
                    Tuple const& t,
                    std::index_sequence<Is...>) {
    os << '(';
    bool first         = true;
    auto print_element = [&](auto i) {
        if(!first) { os << ", "; }
        first = false;
        aglio::ostream::detail::print_value(os, std::get<decltype(i)::value>(t));
    };
    (print_element(std::integral_constant<std::size_t, Is>{}), ...);
    os << ')';
    return os;
}

// Helper for range-based containers
template<typename Stream,
         typename Range>
Stream& print_range(Stream&      os,
                    Range const& r) {
    os << '[';
    bool first = true;
    for(auto const& elem : r) {
        if(!first) { os << ", "; }
        first = false;
        aglio::ostream::detail::print_value(os, elem);
    }
    os << ']';
    return os;
}
}   // namespace aglio::ostream::detail
#endif

#ifdef AGLIO_OSTREAM_DEFINE_STD

// std::pair
template<typename Stream,
         typename T1,
         typename T2>
Stream& operator<<(Stream&              os,
                   std::pair<T1,
                             T2> const& p) {
    aglio::ostream::detail::print_tuple(os, p, std::make_index_sequence<2>{});
    return os;
}

// std::tuple
template<typename Stream,
         typename... Ts>
Stream& operator<<(Stream&                  os,
                   std::tuple<Ts...> const& t) {
    aglio::ostream::detail::print_tuple(os, t, std::make_index_sequence<sizeof...(Ts)>{});
    return os;
}

// std::optional
template<typename Stream,
         typename T>
Stream& operator<<(Stream&                 os,
                   std::optional<T> const& opt) {
    if(opt.has_value()) {
        os << "optional(";
        aglio::ostream::detail::print_value(os, *opt);
        os << ")";
    } else {
        os << "none";
    }
    return os;
}

// std::variant
template<typename Stream,
         typename... Ts>
Stream& operator<<(Stream&                    os,
                   std::variant<Ts...> const& v) {
    os << "variant(";
    std::visit([&](auto const& value) { aglio::ostream::detail::print_value(os, value); }, v);
    os << ")";
    return os;
}

// std::vector
template<typename Stream,
         typename T,
         typename... Args>
Stream& operator<<(Stream&                     os,
                   std::vector<T,
                               Args...> const& v) {
    aglio::ostream::detail::print_range(os, v);
    return os;
}

// std::array
template<typename Stream,
         typename T,
         std::size_t N>
Stream& operator<<(Stream&              os,
                   std::array<T,
                              N> const& a) {
    aglio::ostream::detail::print_range(os, a);
    return os;
}

// std::set
template<typename Stream,
         typename T,
         typename... Args>
Stream& operator<<(Stream&                  os,
                   std::set<T,
                            Args...> const& s) {
    os << '{';
    bool first = true;
    for(auto const& value : s) {
        if(!first) { os << ", "; }
        first = false;
        aglio::ostream::detail::print_value(os, value);
    }
    os << '}';
    return os;
}

// std::map
template<typename Stream,
         typename K,
         typename V,
         typename... Args>
Stream& operator<<(Stream&                  os,
                   std::map<K,
                            V,
                            Args...> const& m) {
    os << '{';
    bool first = true;
    for(auto const& [key, value] : m) {
        if(!first) { os << ", "; }
        first = false;
        aglio::ostream::detail::print_value(os, key);
        os << ": ";
        aglio::ostream::detail::print_value(os, value);
    }
    os << '}';
    return os;
}

#endif   // AGLIO_OSTREAM_DEFINE_STD
