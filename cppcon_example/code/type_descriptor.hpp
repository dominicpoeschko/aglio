#pragma once

template<typename T>
struct TypeDescriptor;

template<auto... MemberPointers>
struct MemberList {
    template<typename T, typename F>
    constexpr static auto apply(F f, T& v) {
        return f(v.*MemberPointers...);
    }
};

template<typename T>
concept Described = requires{TypeDescriptor<T>{};};

