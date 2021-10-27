#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace aglio {
template<typename T>
struct TypeDescriptorGen;

namespace detail {
    template<std::size_t N>
    struct StringLiteral {
        constexpr StringLiteral(char const (&str)[N + 1]) { std::copy_n(str, N, data.data()); }
        constexpr StringLiteral(std::array<char, N> const& data_) : data{data_} {}
        std::array<char, N> data;
    };

    template<std::size_t N>
    StringLiteral(char const (&)[N]) -> StringLiteral<N - 1>;
    template<std::size_t N>
    StringLiteral(std::array<char, N>) -> StringLiteral<N>;

    struct MemberListTag {};
    struct BaseClassListTag {};
}   // namespace detail

template<auto MemberPointer_, detail::StringLiteral Name_>
struct MemberDescriptor {
    static constexpr auto             MemberPointer = MemberPointer_;
    static constexpr std::string_view Name{Name_.data.data(), Name_.data.size()};
};

template<typename... MemberDescriptors>
struct MemberList : detail::MemberListTag {
    template<typename T, typename F>
    constexpr static auto member_apply([[maybe_unused]] F f, T& type) {
        return f(type.*MemberDescriptors::MemberPointer...);
    }

    template<typename T, typename F>
    constexpr static auto member_apply_named([[maybe_unused]] F f, T& type) {
        return f(std::tie(type.*MemberDescriptors::MemberPointer, MemberDescriptors::Name)...);
    }
};

template<typename... BaseClasses>
struct BaseClassList;

namespace detail {
    template<typename T>
    struct Empty {};

    template<typename TDGen>
    struct TypeDescriptorImpl
      : TDGen
      , std::conditional_t<
          std::is_base_of_v<BaseClassListTag, TDGen>,
          Empty<struct A>,
          BaseClassList<>>
      , std::conditional_t<std::is_base_of_v<MemberListTag, TDGen>, Empty<struct B>, MemberList<>> {
    };
}   // namespace detail
template<typename T>
using TypeDescriptor = detail::TypeDescriptorImpl<TypeDescriptorGen<T>>;

template<typename... BaseClasses>
struct BaseClassList : detail::BaseClassListTag {
    template<typename T, typename F>
    constexpr static auto base_class_apply([[maybe_unused]] F f, T& child) {
        return f(
          static_cast<std::conditional_t<std::is_const_v<T>, BaseClasses const&, BaseClasses&>>(
            child)...);
    }

    template<typename T, typename F>
    constexpr static auto base_class_apply_named([[maybe_unused]] F f, T& child) {
        auto named = [](auto& base) {
            using Base = std::remove_cvref_t<decltype(base)>;
            if constexpr(requires { TypeDescriptor<Base>{}; }) {
                return std::tie(base, TypeDescriptor<Base>::Name);
            } else {
                constexpr std::string_view empty{};
                return std::tie(base, empty);
            }
        };
        return f(named(
          static_cast<std::conditional_t<std::is_const_v<T>, BaseClasses const&, BaseClasses&>>(
            child))...);
    }
};

namespace detail {
    template<typename, typename = void>
    static constexpr bool is_type_complete_v = false;

    template<typename T>
    static constexpr bool is_type_complete_v<T, std::void_t<decltype(sizeof(T))>> = true;
}   // namespace detail
template<typename T>
struct has_type_descriptor {
    static constexpr bool value = detail::is_type_complete_v<TypeDescriptorGen<T>>;
};

template<typename T>
static constexpr bool has_type_descriptor_v{has_type_descriptor<T>::value};

template<typename T>
concept Described = has_type_descriptor_v<T>;

}   // namespace aglio
