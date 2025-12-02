#pragma once

#include "type_descriptor.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>
#include <variant>

namespace aglio {
namespace detail {

    template<typename T>
    concept trivial = std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>;

    template<typename T>
    concept is_tuple_like = requires {
        { std::tuple_size<T>::value } -> std::convertible_to<std::size_t>;
    };

    template<typename T>
    concept is_tuple_like_but_not_range = is_tuple_like<T> && !std::ranges::range<T>;

    template<typename T>
    concept is_map = requires {
        typename T::mapped_type;
        typename T::key_type;
        typename T::value_type;
    };

    template<typename T>
    concept is_set = requires {
        typename T::key_type;
        typename T::value_type;
    } && !is_map<T>;

    template<typename T>
    struct remove_pair_const;

    template<typename K, typename V>
    struct remove_pair_const<std::pair<K, V>> {
        using type = std::pair<std::remove_const_t<K>, V>;
    };

    template<typename T>
    struct associative_container_value_type;

    template<is_set T>
    struct associative_container_value_type<T> {
        using type = typename T::value_type;
    };

    template<is_map T>
    struct associative_container_value_type<T> {
        using type = typename remove_pair_const<typename T::value_type>::type;
    };

}   // namespace detail

template<typename T, typename Size_t>
struct serializer;

template<detail::trivial T, typename Size_t>
struct serializer<T, Size_t> {
    template<typename Buffer>
    static constexpr bool serialize(T const& v,
                                    Buffer&  buffer) {
        return buffer.insert(std::as_bytes(std::span{std::addressof(v), 1}));
    }

    template<typename Buffer>
    static constexpr bool deserialize(T&      v,
                                      Buffer& buffer) {
        return buffer.extract(std::as_writable_bytes(std::span{std::addressof(v), 1}));
    }
};

template<Described T, typename Size_t>
    requires(!std::ranges::range<T>)
struct serializer<T, Size_t> {
    template<typename Buffer>
    static constexpr bool serialize(T const& v,
                                    Buffer&  buffer) {
        auto const tie = glz::to_tie(v);
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (serializer<std::remove_cvref_t<decltype(get<Is>(tie))>, Size_t>::serialize(
                      get<Is>(tie),
                      buffer)
                    && ...);
        }(std::make_index_sequence<glz::reflect<T>::size>{});
    }

    template<typename Buffer>
    static constexpr bool deserialize(T&      v,
                                      Buffer& buffer) {
        auto tie = glz::to_tie(v);
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (serializer<std::remove_cvref_t<decltype(get<Is>(tie))>, Size_t>::deserialize(
                      get<Is>(tie),
                      buffer)
                    && ...);
        }(std::make_index_sequence<glz::reflect<T>::size>{});
    }
};

template<typename T, typename Size_t>
struct serializer<std::optional<T>, Size_t> {
    template<typename Buffer>
    static constexpr bool serialize(std::optional<T> const& v,
                                    Buffer&                 buffer) {
        if(!serializer<bool, Size_t>::serialize(v.has_value(), buffer)) { return false; }
        if(v.has_value()) { return serializer<T, Size_t>::serialize(*v, buffer); }
        return true;
    }

    template<typename Buffer>
    static constexpr bool deserialize(std::optional<T>& v,
                                      Buffer&           buffer) {
        bool has_value{};
        if(!serializer<bool, Size_t>::deserialize(has_value, buffer)) { return false; }
        if(has_value) {
            v.emplace();
            return serializer<T, Size_t>::deserialize(*v, buffer);
        }
        v = std::nullopt;
        return true;
    }
};

template<typename... Ts, typename Size_t>
struct serializer<std::variant<Ts...>, Size_t> {
    template<typename Buffer>
    static constexpr bool serialize(std::variant<Ts...> const& v,
                                    Buffer&                    buffer) {
        constexpr std::size_t N{sizeof...(Ts)};
        using Index_t = std::
          conditional_t<(N > std::numeric_limits<std::uint8_t>::max()), Size_t, std::uint8_t>;
        static_assert(std::numeric_limits<Index_t>::max() >= N, "variant to big");
        Index_t const index = static_cast<Index_t>(v.index());
        if(!serializer<Index_t, Size_t>::serialize(index, buffer)) { return false; }
        return std::visit(
          [&](auto const& vv) {
              return serializer<std::remove_cvref_t<decltype(vv)>, Size_t>::serialize(vv, buffer);
          },
          v);
    }

    template<typename Buffer>
    static constexpr bool deserialize(std::variant<Ts...>& v,
                                      Buffer&              buffer) {
        constexpr std::size_t N{sizeof...(Ts)};
        using Index_t = std::
          conditional_t<(N > std::numeric_limits<std::uint8_t>::max()), Size_t, std::uint8_t>;
        static_assert(std::numeric_limits<Index_t>::max() >= N, "variant to big");
        Index_t index{};

        if(!serializer<Index_t, Size_t>::deserialize(index, buffer)) { return false; }
        if(index >= N) { return false; }

        auto action = [&](auto i) {
            static constexpr std::size_t I = decltype(i)::value;
            if(I == index) {
                v.template emplace<I>();
                return serializer<std::variant_alternative_t<I, std::variant<Ts...>>,
                                  Size_t>::deserialize(std::get<I>(v), buffer);
            }
            return N > I;
        };

        return [&]<std::size_t... Ns>(std::index_sequence<Ns...>) {
            return (action(std::integral_constant<std::size_t, Ns>{}) && ...);
        }(std::make_index_sequence<N>{});
    }
};

template<typename Rep, typename Period, typename Size_t>
struct serializer<std::chrono::duration<Rep, Period>, Size_t> {
    template<typename Buffer>
    static constexpr bool serialize(std::chrono::duration<Rep,
                                                          Period> const& v,
                                    Buffer&                              buffer) {
        return serializer<Rep, Size_t>::serialize(v.count(), buffer);
    }

    template<typename Buffer>
    static constexpr bool deserialize(std::chrono::duration<Rep,
                                                            Period>& v,
                                      Buffer&                        buffer) {
        Rep vv;
        if(!serializer<Rep, Size_t>::deserialize(vv, buffer)) { return false; }
        v = std::chrono::duration<Rep, Period>{vv};
        return true;
    }
};

template<detail::is_tuple_like_but_not_range T, typename Size_t>
struct serializer<T, Size_t> {
    template<typename Buffer>
    static constexpr bool serialize(T const& v,
                                    Buffer&  buffer) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (serializer<std::tuple_element_t<Is, T>, Size_t>::serialize(get<Is>(v), buffer)
                    && ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }

    template<typename Buffer>
    static constexpr bool deserialize(T&      v,
                                      Buffer& buffer) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (serializer<std::tuple_element_t<Is, T>, Size_t>::deserialize(get<Is>(v), buffer)
                    && ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }
};

template<std::ranges::range T, typename Size_t>
struct serializer<T, Size_t> {
    static constexpr bool is_contiguous = std::ranges::contiguous_range<T>;
    using value_t                       = std::ranges::range_value_t<T>;
    static constexpr bool is_trivial    = detail::trivial<value_t>;

    template<typename Buffer>
    static constexpr bool serialize(T const& v,
                                    Buffer&  buffer) {
        auto const full_size = std::ranges::size(v);
        // Check for overflow when Size_t is smaller than the range's size type
        if constexpr(std::numeric_limits<Size_t>::max()
                     < std::numeric_limits<decltype(full_size)>::max())
        {
            if(full_size > std::numeric_limits<Size_t>::max()) {
                return false;   // Size exceeds Size_t capacity
            }
        }
        auto const size = static_cast<Size_t>(full_size);

        if(!serializer<Size_t, Size_t>::serialize(size, buffer)) { return false; }

        if constexpr(is_contiguous && is_trivial) {
            return buffer.insert(std::as_bytes(std::span{v}));
        } else {
            for(auto const& vv : v) {
                if(!serializer<value_t, Size_t>::serialize(vv, buffer)) { return false; }
            }
            return true;
        }
    }

    template<typename Buffer>
    static constexpr bool deserialize(T&      v,
                                      Buffer& buffer) {
        Size_t size{};
        if(!serializer<Size_t, Size_t>::deserialize(size, buffer)) { return false; }
        if(size > buffer.size()) { return false; }

        if constexpr(requires { v.resize(size); }) { v.resize(size); }

        if constexpr(detail::is_map<T> || detail::is_set<T>) {
            v.clear();
        } else {
            if(std::ranges::size(v) != size) { return false; }
        }

        if constexpr(is_contiguous && is_trivial) {
            return buffer.extract(std::as_writable_bytes(std::span{v}));
        } else {
            if constexpr(detail::is_map<T> || detail::is_set<T>) {
                while(size != 0) {
                    --size;
                    using value_type = typename detail::associative_container_value_type<T>::type;

                    value_type vv;
                    if(!serializer<value_type, Size_t>::deserialize(vv, buffer)) { return false; }
                    v.insert(vv);
                }
            } else {
                for(auto& vv : v) {
                    if(!serializer<value_t, Size_t>::deserialize(vv, buffer)) { return false; }
                }
            }
            return true;
        }
    }
};

template<typename Size_t>
struct Serializer {
    template<typename Buffer,
             typename... Ts>
    static constexpr bool serialize(Buffer& buffer,
                                    Ts const&... vs) {
        return (serializer<std::remove_cvref_t<Ts>, Size_t>::serialize(vs, buffer) && ...);
    }

    template<typename Buffer,
             typename... Ts>
    static constexpr bool deserialize(Buffer& buffer,
                                      Ts&... vs) {
        return (serializer<std::remove_cvref_t<Ts>, Size_t>::deserialize(vs, buffer) && ...);
    }

    template<typename T,
             typename Buffer>
    static constexpr std::optional<T> deserialize(Buffer& buffer) {
        std::optional<T> v;
        v.emplace();
        if(!serializer<std::remove_cvref_t<T>, Size_t>::deserialize(*v, buffer)) {
            v = std::nullopt;
        }
        return v;
    }
};
}   // namespace aglio
