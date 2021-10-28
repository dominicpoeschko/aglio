#pragma once

#include "type_descriptor.hpp"

template<typename OStream, Described T>
OStream& operator<<(OStream& os, T const& v) {
    os << '(';
    TypeDescriptor<T>::apply(
        [&os](auto const&... members) {
            ((os << " " << members), ...);
        }
      , v);
    os << " )";
    return os;
}

