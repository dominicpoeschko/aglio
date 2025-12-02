#pragma once
#include "type_descriptor.hpp"

#ifdef AGLIO_OSTREAM_DEFINE_STD
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

template<typename Stream,
         aglio::Described T>
constexpr Stream& operator<<(Stream&  os,
                             T const& v) {
    os << glz::type_name<T>;
    os << '(';

    constexpr auto N   = glz::reflect<T>::size;
    auto const     tie = glz::to_tie(v);

    bool first = true;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        using std::get;
        auto process = [&]<std::size_t I>() {
            constexpr auto name  = glz::reflect<T>::keys[I];
            auto const&    value = get<I>(tie);

            if(!first) {
                os << ", ";
            } else {
                first = false;
            }
            os << '\"';
            os << name;
            os << "\": ";
            using namespace aglio;
            using namespace std;
#if defined(__clang__)
            os << value;
#elif defined(__GNUC__) || defined(__GNUG__)
            if constexpr(requires { os << value; }) {
                os << value;
            } else {
                operator<<(os, value);
            }
#endif
        };
        (process.template operator()<Is>(), ...);
    }(std::make_index_sequence<N>{});

    os << ')';
    return os;
}

#ifdef AGLIO_OSTREAM_DEFINE_STD

// std::pair
template<typename Stream,
         typename T1,
         typename T2>
Stream& operator<<(Stream&              os,
                   std::pair<T1,
                             T2> const& p) {
    return os << '(' << p.first << ", " << p.second << ')';
}

// std::tuple
template<typename Stream,
         typename... Ts>
Stream& operator<<(Stream&                  os,
                   std::tuple<Ts...> const& t) {
    os << '(';
    bool first = true;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (([&]() {
             if(!first) { os << ", "; }
             first = false;
             os << std::get<Is>(t);
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});
    return os << ')';
}

// std::optional
template<typename Stream,
         typename T>
Stream& operator<<(Stream&                 os,
                   std::optional<T> const& opt) {
    if(opt.has_value()) {
        return os << "some(" << *opt << ')';
    } else {
        return os << "nullopt";
    }
}

// std::variant
template<typename Stream,
         typename... Ts>
Stream& operator<<(Stream&                    os,
                   std::variant<Ts...> const& v) {
    os << "variant<" << v.index() << ">(";
    std::visit([&](auto const& value) { os << value; }, v);
    return os << ')';
}

// std::vector
template<typename Stream,
         typename T,
         typename... Args>
Stream& operator<<(Stream&                     os,
                   std::vector<T,
                               Args...> const& v) {
    os << '[';
    bool first = true;
    for(auto const& elem : v) {
        if(!first) { os << ", "; }
        first = false;
        os << elem;
    }
    return os << ']';
}

// std::array
template<typename Stream,
         typename T,
         std::size_t N>
Stream& operator<<(Stream&              os,
                   std::array<T,
                              N> const& a) {
    os << '[';
    bool first = true;
    for(auto const& elem : a) {
        if(!first) { os << ", "; }
        first = false;
        os << elem;
    }
    return os << ']';
}

// std::set
template<typename Stream,
         typename T,
         typename... Args>
Stream& operator<<(Stream&                  os,
                   std::set<T,
                            Args...> const& s) {
    os << '[';
    bool first = true;
    for(auto const& elem : s) {
        if(!first) { os << ", "; }
        first = false;
        os << elem;
    }
    return os << ']';
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
        os << key << ": " << value;
    }
    return os << '}';
}

#endif   // AGLIO_OSTREAM_DEFINE_STD
