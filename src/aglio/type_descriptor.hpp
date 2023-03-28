#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>

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
    static constexpr std::size_t N_Members{sizeof...(MemberDescriptors)};
    template<typename T, typename F>
    constexpr static auto member_apply([[maybe_unused]] F&& f, T& type) {
        return std::invoke(std::forward<F>(f), type.*MemberDescriptors::MemberPointer...);
    }

    template<typename T, typename F>
    constexpr static auto member_apply_named([[maybe_unused]] F&& f, T& type) {
        return std::invoke(
          std::forward<F>(f),
          std::tie(MemberDescriptors::Name, type.*MemberDescriptors::MemberPointer)...);
    }

    template<typename T>
    constexpr static auto get_member_tuple(T& type) {
        return std::tie(type.*MemberDescriptors::MemberPointer...);
    }

    template<typename T>
    constexpr static auto get_member_named_tuple(T& type) {
        return std::make_tuple(
          std::tie(MemberDescriptors::Name, type.*MemberDescriptors::MemberPointer)...);
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
        using base_base
          = std::conditional_t<std::is_base_of_v<BaseClassListTag, TDGen>, TDGen, BaseClassList<>>;

        using member_base
          = std::conditional_t<std::is_base_of_v<MemberListTag, TDGen>, TDGen, MemberList<>>;

        template<typename T, typename F>
        constexpr static auto apply([[maybe_unused]] F&& f, T& type) {
            return std::apply(
              std::forward<F>(f),
              std::tuple_cat(
                base_base::get_base_class_tuple(type),
                member_base::get_member_tuple(type)));
        }

        template<typename T, typename F>
        constexpr static auto apply_named([[maybe_unused]] F&& f, T& type) {
            return std::apply(
              std::forward<F>(f),
              std::tuple_cat(
                base_base::get_base_class_named_tuple(type),
                member_base::get_member_named_tuple(type)));
        }

        template<typename T>
        constexpr static auto get_tuple(T& type) {
            return std::tuple_cat(
              base_base::get_base_class_tuple(type),
              member_base::get_member_tuple(type));
        }

        template<typename T>
        constexpr static auto get_named_tuple(T& type) {
            return std::tuple_cat(
              base_base::get_base_class_named_tuple(type),
              member_base::get_member_named_tuple(type));
        }
    };
}   // namespace detail
template<typename T>
using TypeDescriptor = detail::TypeDescriptorImpl<TypeDescriptorGen<T>>;

template<typename... BaseClasses>
struct BaseClassList : detail::BaseClassListTag {
    static constexpr std::size_t N_BaseClasses{sizeof...(BaseClasses)};
    template<typename T, typename F>
    constexpr static auto base_class_apply([[maybe_unused]] F&& f, T& child) {
        return std::invoke(
          std::forward<F>(f),
          static_cast<std::conditional_t<std::is_const_v<T>, BaseClasses const&, BaseClasses&>>(
            child)...);
    }

    template<typename T, typename F>
    constexpr static auto base_class_apply_named([[maybe_unused]] F&& f, T& child) {
        [[maybe_unused]] auto named = [](auto& base) {
            using Base = std::remove_cvref_t<decltype(base)>;
            if constexpr(requires { TypeDescriptor<Base>{}; }) {
                return std::tie(TypeDescriptor<Base>::Name, base);
            } else {
                constexpr std::string_view empty{};
                return std::tie(empty, base);
            }
        };
        return std::invoke(
          std::forward<F>(f),
          named(
            static_cast<std::conditional_t<std::is_const_v<T>, BaseClasses const&, BaseClasses&>>(
              child))...);
    }

    template<typename T>
    constexpr static auto get_base_class_tuple(T& child) {
        return std::tie(
          static_cast<std::conditional_t<std::is_const_v<T>, BaseClasses const&, BaseClasses&>>(
            child)...);
    }

    template<typename T>
    constexpr static auto get_base_class_named_tuple(T& child) {
        [[maybe_unused]] auto named = [](auto& base) {
            using Base = std::remove_cvref_t<decltype(base)>;
            if constexpr(requires { TypeDescriptor<Base>{}; }) {
                return std::tie(TypeDescriptor<Base>::Name, base);
            } else {
                constexpr std::string_view empty{};
                return std::tie(empty, base);
            }
        };
        return std::make_tuple(named(
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
