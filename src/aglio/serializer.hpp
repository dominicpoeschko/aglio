#pragma once

#include "type_descriptor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace aglio {
namespace detail {

    template<typename Buffer, typename... Ts>
    constexpr bool serializeDispatch(Buffer& buffer, Ts const&... vs);

    template<typename Buffer, typename... Ts>
    constexpr bool deserializeDispatch(Buffer& buffer, Ts&... vs);

    namespace impl {

        using Size_t = std::uint32_t;

        template<typename Buffer, typename C>
        constexpr bool serializeContainer(Buffer& buffer, C const& v) {
            for(auto const& vv : v) {
                if(!serializeDispatch(buffer, vv)) {
                    return false;
                }
            }
            return true;
        }
        template<typename Buffer, typename C>
        constexpr bool deserializeContainer(Buffer& buffer, C& v) {
            for(auto& vv : v) {
                if(!deserializeDispatch(buffer, vv)) {
                    return false;
                }
            }
            return true;
        }

        template<typename Buffer, typename T>
        constexpr bool serialize(Buffer& buffer, std::vector<T> const& v) {
            Size_t size = static_cast<Size_t>(v.size());
            if(!serializeDispatch(buffer, size)) {
                return false;
            }
            return serializeContainer(buffer, v);
        }

        template<typename Buffer, typename T>
        constexpr bool deserialize(Buffer& buffer, std::vector<T>& v) {
            Size_t size{};
            if(!deserializeDispatch(buffer, size)) {
                return false;
            }
            v.resize(size);
            return deserializeContainer(buffer, v);
        }

        template<typename Buffer, typename K, typename V>
        constexpr bool serialize(Buffer& buffer, std::map<K, V> const& v) {
            Size_t size = static_cast<Size_t>(v.size());
            if(!serializeDispatch(buffer, size)) {
                return false;
            }
            return serializeContainer(buffer, v);
        }
        template<typename Buffer, typename K, typename V>
        constexpr bool deserialize(Buffer& buffer, std::map<K, V>& v) {
            Size_t size{};
            if(!deserializeDispatch(buffer, size)) {
                return false;
            }
            v.clear();
            while(size != 0) {
                --size;
                std::pair<K, V> vv;
                if(!deserializeDispatch(buffer, vv)) {
                    return false;
                }
                v.insert(vv);
            }
            return true;
        }

        template<typename Buffer>
        constexpr bool serialize(Buffer& buffer, std::string const& v) {
            Size_t size = static_cast<Size_t>(v.size());
            if(!serializeDispatch(buffer, size)) {
                return false;
            }
            return serializeContainer(buffer, v);
        }
        template<typename Buffer>
        constexpr bool deserialize(Buffer& buffer, std::string& v) {
            Size_t size{};
            if(!deserializeDispatch(buffer, size)) {
                return false;
            }
            v.resize(size);
            return deserializeContainer(buffer, v);
        }

        template<typename Buffer, typename T, std::size_t N>
        constexpr bool serialize(Buffer& buffer, std::array<T, N> const& v) {
            return serializeContainer(buffer, v);
        }
        template<typename Buffer, typename T, std::size_t N>
        constexpr bool deserialize(Buffer& buffer, std::array<T, N>& v) {
            return deserializeContainer(buffer, v);
        }

        template<typename Buffer, typename T>
        constexpr bool serialize(Buffer& buffer, std::optional<T> const& v) {
            if(!serializeDispatch(buffer, v.has_value())) {
                return false;
            }
            if(v) {
                return serializeDispatch(buffer, *v);
            }
            return true;
        }
        template<typename Buffer, typename T>
        constexpr bool deserialize(Buffer& buffer, std::optional<T>& v) {
            bool has_value{};
            if(!deserializeDispatch(buffer, has_value)) {
                return false;
            }
            if(has_value) {
                v = T{};
                return deserializeDispatch(buffer, *v);
            }
            v.reset();
            return true;
        }

        template<typename Buffer, typename... Ts>
        constexpr bool serialize(Buffer& buffer, std::variant<Ts...> const& v) {
            constexpr std::size_t N{sizeof...(Ts)};
            using Index_t = std::
              conditional_t<(N > std::numeric_limits<std::uint8_t>::max()), Size_t, std::uint8_t>;
            Index_t const index = static_cast<Index_t>(v.index());
            if(!serializeDispatch(buffer, index)) {
                return false;
            }
            return std::visit([&](auto const& vv) { return serializeDispatch(buffer, vv); }, v);
        }

        template<std::size_t... Ns, typename F>
        constexpr auto variant_loop(std::index_sequence<Ns...>, F f) {
            return (f(std::integral_constant<std::size_t, Ns>{}) && ...);
        }

        template<typename Buffer, typename... Ts>
        constexpr bool deserialize(Buffer& buffer, std::variant<Ts...>& v) {
            constexpr std::size_t N{sizeof...(Ts)};
            using Index_t = std::
              conditional_t<(N > std::numeric_limits<std::uint8_t>::max()), Size_t, std::uint8_t>;
            Index_t index{};
            if(!deserializeDispatch(buffer, index)) {
                return false;
            }
            if(index >= N) {
                return false;
            }

            auto action = [&](auto i) {
                static constexpr std::size_t I = decltype(i)::value;
                if(I == index) {
                    v = std::variant_alternative_t<I, std::variant<Ts...>>{};
                    return deserializeDispatch(buffer, std::get<I>(v));
                }
                return N > I;
            };
            return variant_loop(std::make_index_sequence<N>{}, action);
        }

        template<typename Buffer, typename... Ts>
        constexpr bool serialize(Buffer& buffer, std::tuple<Ts...> const& v) {
            auto call = [&](auto const&... vv) { return serializeDispatch(buffer, vv...); };
            return std::apply(call, v);
        }
        template<typename Buffer, typename... Ts>
        constexpr bool deserialize(Buffer& buffer, std::tuple<Ts...>& v) {
            auto call = [&](auto&... vv) { return deserializeDispatch(buffer, vv...); };
            return std::apply(call, v);
        }

        template<typename Buffer, typename T1, typename T2>
        constexpr bool serialize(Buffer& buffer, std::pair<T1, T2> const& v) {
            return serializeDispatch(buffer, v.first, v.second);
        }
        template<typename Buffer, typename T1, typename T2>
        constexpr bool deserialize(Buffer& buffer, std::pair<T1, T2>& v) {
            return deserializeDispatch(buffer, v.first, v.second);
        }

        template<typename Buffer, typename Rep, typename Period>
        constexpr bool serialize(Buffer& buffer, std::chrono::duration<Rep, Period> const& v) {
            return serializeDispatch(buffer, v.count());
        }
        template<typename Buffer, typename Rep, typename Period>
        constexpr bool deserialize(Buffer& buffer, std::chrono::duration<Rep, Period>& v) {
            Rep vv;
            if(!deserializeDispatch(buffer, vv)) {
                return false;
            }
            v = std::chrono::duration<Rep, Period>{vv};
            return true;
        }

        template<typename T>
        concept trivial = std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>;

        template<typename Buffer, trivial T>
        constexpr bool serialize(Buffer& buffer, T const& v) {
            return buffer.insertBytes(
              reinterpret_cast<std::byte const*>(std::addressof(v)),
              sizeof(v));
        }

        template<typename Buffer, trivial T>
        constexpr bool deserialize(Buffer& buffer, T& v) {
            return buffer.extractBytes(reinterpret_cast<std::byte*>(std::addressof(v)), sizeof(v));
        }

        template<typename Buffer, Described T>
        constexpr bool serialize(Buffer& buffer, T const& v) {
            auto call = [&](auto const&... vv) { return serializeDispatch(buffer, vv...); };
            return TypeDescriptor<T>::base_class_apply(call, v)
                && TypeDescriptor<T>::member_apply(call, v);
        }

        template<typename Buffer, Described T>
        constexpr bool deserialize(Buffer& buffer, T& v) {
            auto call = [&](auto&... vv) { return deserializeDispatch(buffer, vv...); };
            return TypeDescriptor<T>::base_class_apply(call, v)
                && TypeDescriptor<T>::member_apply(call, v);
        }
    }   // namespace impl
    template<typename Buffer, typename... Ts>
    constexpr bool serializeDispatch(Buffer& buffer, Ts const&... vs) {
        return (impl::serialize(buffer, vs) && ...);
    }

    template<typename Buffer, typename... Ts>
    constexpr bool deserializeDispatch(Buffer& buffer, Ts&... vs) {
        return (impl::deserialize(buffer, vs) && ...);
    }
}   // namespace detail

struct Serializer {
    template<typename Buffer, typename... Ts>
    static constexpr bool serialize(Buffer& buffer, Ts const&... vs) {
        return detail::serializeDispatch(buffer, vs...);
    }

    template<typename Buffer, typename... Ts>
    static constexpr bool deserialize(Buffer& buffer, Ts&... vs) {
        return detail::deserializeDispatch(buffer, vs...);
    }
};
}   // namespace aglio
