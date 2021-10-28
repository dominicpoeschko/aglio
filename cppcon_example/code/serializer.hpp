#pragma once

#include "type_descriptor.hpp"
#include <type_traits>
#include <memory>
#include <cstddef>
#include <cstring>
#include <concepts>

template<typename Buffer, typename... Ts>
bool deserialize(Buffer& buffer, Ts&... vs);

template<typename Buffer, std::integral T>
bool deserialize_impl(Buffer& buffer, T& v) {
    static constexpr std::size_t typeSize = sizeof(T);
    if(typeSize > buffer.size()){
        return false;
    }
    std::memcpy(std::addressof(v), buffer.data(), typeSize);
    buffer.erase(buffer.begin(), std::next(buffer.begin(), typeSize));
    return true;
}

template<typename Buffer, Described T>
bool deserialize_impl(Buffer& buffer, T& v) {
    return TypeDescriptor<T>::apply(
        [&buffer](auto&... members) {
            return deserialize(buffer, members...);
        }
      , v);
}

template<typename Buffer, typename... Ts>
bool deserialize(Buffer& buffer, Ts&... vs) {
    return (deserialize_impl(buffer, vs) && ...);
}

template<typename Buffer, typename... Ts>
bool serialize(Buffer& buffer, Ts const&... vs);

template<typename Buffer, std::integral T>
bool serialize_impl(Buffer& buffer, T const& v) {
    static constexpr std::size_t typeSize = sizeof(T);
    auto const oldSize        = buffer.size();
    buffer.resize(oldSize + typeSize);
    std::memcpy(buffer.data() + oldSize, std::addressof(v), typeSize);
    return true;
}

template<typename Buffer, Described T>
bool serialize_impl(Buffer& buffer, T const& v) {
    return TypeDescriptor<T>::apply(
        [&buffer](auto const&... members) {
            return serialize(buffer, members...);
        }
      , v
    );
}

template<typename Buffer, typename... Ts>
bool serialize(Buffer& buffer, Ts const&... vs) {
    return (serialize_impl(buffer, vs) && ...);
}

