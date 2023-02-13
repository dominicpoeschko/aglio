#pragma once
#include "type_descriptor.hpp"

template<typename Stream, aglio::Described T>
constexpr Stream& operator<<(Stream& os, T const& v) {
    using td = aglio::TypeDescriptor<T>;
    os << td::Name;
    os << '(';
    bool                  first = true;
    [[maybe_unused]] auto call  = [&](auto const&... name_values) {
        [[maybe_unused]] auto sub_call = [&](auto const& name_value) {
            auto const& [name, value] = name_value;
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
        (sub_call(name_values), ...);
    };

    td::apply_named(call, v);
    os << ')';
    return os;
}
