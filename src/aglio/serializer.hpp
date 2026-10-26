#pragma once

#include "inline.hpp"
#include "type_descriptor.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace aglio {
namespace detail {

    template<typename T>
    concept Trivial = std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>;

    template<typename T>
    concept IsTupleLike = requires {
        // NOLINTNEXTLINE(modernize-type-traits) // tuple_size_v<T> is a hard error, not false, for a non-tuple
        { std::tuple_size<T>::value } -> std::convertible_to<std::size_t>;
    };

    template<typename T>
    concept IsTupleLikeButNotRange = IsTupleLike<T> && !std::ranges::range<T>;

    template<typename T>
    concept IsMap = requires {
        typename T::mapped_type;
        typename T::key_type;
        typename T::value_type;
    };

    template<typename T>
    concept IsSet = requires {
        typename T::key_type;
        typename T::value_type;
    } && !IsMap<T>;

    template<typename T>
    struct remove_pair_const;

    template<typename K, typename V>
    struct remove_pair_const<std::pair<K, V>> {
        using type = std::pair<std::remove_const_t<K>, V>;
    };

    template<typename T>
    struct associative_container_value_type;

    template<IsSet T>
    struct associative_container_value_type<T> {
        using type = typename T::value_type;
    };

    template<IsMap T>
    struct associative_container_value_type<T> {
        using type = typename remove_pair_const<typename T::value_type>::type;
    };

}   // namespace detail

template<typename T, typename SizeT>
struct serializer;

template<typename T, typename SizeT>
struct serialized_size;

template<typename T, typename SizeT>
concept HasFixedSerializedSize = requires {
    { serialized_size<T, SizeT>::value } -> std::convertible_to<std::size_t>;
};

namespace detail {

    /// The N bytes claimed for a value of fixed serialized size: one bounds check for the whole
    /// value instead of one per field.
    template<std::size_t N>
    struct ClaimedForWrite {
        std::byte*  to;
        std::size_t used{};

        template<std::size_t M>
            requires(M != std::dynamic_extent)
        AGLIO_INLINE constexpr bool insert(std::span<std::byte const,
                                                     M> data) {
            if constexpr(M != 0) {
                if(M > N - used) { return false; }
                copyFixed<M>(std::next(to, static_cast<std::make_signed_t<std::size_t>>(used)),
                             data.data());
                used += M;
            }
            return true;
        }

        constexpr bool insert(std::span<std::byte const> data) {
            if(data.size() > N - used) { return false; }
            if(!data.empty()) {
                std::memcpy(std::next(to, static_cast<std::make_signed_t<std::size_t>>(used)),
                            data.data(),
                            data.size());
                used += data.size();
            }
            return true;
        }
    };

    /// The same for reading.
    template<std::size_t N>
    struct ClaimedForRead {
        std::byte const* from;
        std::size_t      used{};

        /// What a range's length is checked against.
        static constexpr std::size_t size() { return N; }

        template<std::size_t M>
            requires(M != std::dynamic_extent)
        AGLIO_INLINE constexpr bool extract(std::span<std::byte,
                                                      M> data) {
            if constexpr(M != 0) {
                if(M > N - used) { return false; }
                copyFixed<M>(data.data(),
                             std::next(from, static_cast<std::make_signed_t<std::size_t>>(used)));
                used += M;
            }
            return true;
        }

        constexpr bool extract(std::span<std::byte> data) {
            if(data.size() > N - used) { return false; }
            if(!data.empty()) {
                std::memcpy(data.data(),
                            std::next(from, static_cast<std::make_signed_t<std::size_t>>(used)),
                            data.size());
                used += data.size();
            }
            return true;
        }
    };

    template<typename Buffer>
    constexpr bool is_claimed_v = false;
    template<std::size_t N>
    constexpr bool is_claimed_v<ClaimedForWrite<N>> = true;
    template<std::size_t N>
    constexpr bool is_claimed_v<ClaimedForRead<N>> = true;

    /// A value of fixed, non-zero serialized size and a buffer with claim(n) that is not already
    /// a claim (nested fixed-size values go through the outer one).
    template<typename T, typename SizeT, typename Buffer>
    concept ClaimsWrite
      = HasFixedSerializedSize<T, SizeT> && (serialized_size<T, SizeT>::value != 0)
     && !is_claimed_v<Buffer> && requires(Buffer& b) {
            { b.claim(std::size_t{}) } -> std::same_as<std::byte*>;
        };

    template<typename T, typename SizeT, typename Buffer>
    concept ClaimsRead = HasFixedSerializedSize<T, SizeT> && (serialized_size<T, SizeT>::value != 0)
                      && !is_claimed_v<Buffer> && requires(Buffer& b) {
                             { b.claim(std::size_t{}) } -> std::same_as<std::byte const*>;
                         };

}   // namespace detail

template<typename SizeT>
struct serializer<bool, SizeT> {
    AGLIO_INLINE static constexpr bool serialize(bool  value,
                                                 auto& buffer) {
        std::byte const data{value};
        return buffer.insert(std::span<std::byte const, 1>{&data, std::size_t{1}});
    }

    AGLIO_INLINE static constexpr bool deserialize(bool& output,
                                                   auto& buffer) {
        std::byte data{};
        if(buffer.extract(std::span<std::byte, 1>{&data, std::size_t{1}})) {
            output = static_cast<bool>(static_cast<std::uint32_t>(data));
            return true;
        }
        return false;
    }
};

template<detail::Trivial T, typename SizeT>
struct serializer<T, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(T const& v,
                                                 Buffer&  buffer) {
        return buffer.insert(std::as_bytes(std::span<T const, 1>{std::addressof(v), 1}));
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(T&      v,
                                                   Buffer& buffer) {
        return buffer.extract(std::as_writable_bytes(std::span<T, 1>{std::addressof(v), 1}));
    }
};

template<Described T, typename SizeT>
struct serializer<T, SizeT> {
    static_assert(
      glz::reflect<T>::size > 0 || std::is_empty_v<T>,
      "Described type has members but glaze reflects 0. "
      "This can happen with reference members. Provide a custom aglio::serializer specialization.");

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(T const& v,
                                                 Buffer&  buffer) {
        constexpr auto Members = std::make_index_sequence<glz::reflect<T>::size>{};
        if constexpr(detail::ClaimsWrite<T, SizeT, Buffer>) {
            constexpr std::size_t N  = serialized_size<T, SizeT>::value;
            std::byte* const      to = buffer.claim(N);
            if(to == nullptr) { return false; }
            detail::ClaimedForWrite<N> claimed{to};
            // A serialized_size that is not what the serializers write is a failure, never a gap.
            return serializeMembers(glz::to_tie(v), claimed, Members) && claimed.used == N;
        } else {
            return serializeMembers(glz::to_tie(v), buffer, Members);
        }
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(T&      v,
                                                   Buffer& buffer) {
        constexpr auto Members = std::make_index_sequence<glz::reflect<T>::size>{};
        if constexpr(detail::ClaimsRead<T, SizeT, Buffer>) {
            constexpr std::size_t  N    = serialized_size<T, SizeT>::value;
            std::byte const* const from = buffer.claim(N);
            if(from == nullptr) { return false; }
            detail::ClaimedForRead<N> claimed{from};
            return deserializeMembers(glz::to_tie(v), claimed, Members) && claimed.used == N;
        } else {
            return deserializeMembers(glz::to_tie(v), buffer, Members);
        }
    }

private:
    // Functions, not lambdas: a lambda cannot be forced inline portably.
    template<typename Tie,
             typename Buffer,
             std::size_t... Is>
    AGLIO_INLINE static constexpr bool serializeMembers(Tie const& tie,
                                                        Buffer&    buffer,
                                                        std::index_sequence<Is...>) {
        using std::get;
        return (
          serializer<std::remove_cvref_t<decltype(get<Is>(tie))>, SizeT>::serialize(get<Is>(tie),
                                                                                    buffer)
          && ...);
    }

    template<typename Tie,
             typename Buffer,
             std::size_t... Is>
    AGLIO_INLINE static constexpr bool deserializeMembers(Tie&&   tie,
                                                          Buffer& buffer,
                                                          std::index_sequence<Is...>) {
        using std::get;
        return (
          serializer<std::remove_cvref_t<decltype(get<Is>(tie))>, SizeT>::deserialize(get<Is>(tie),
                                                                                      buffer)
          && ...);
    }
};

template<typename T, typename SizeT>
struct serializer<std::optional<T>, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::optional<T> const& v,
                                                 Buffer&                 buffer) {
        if(!serializer<bool, SizeT>::serialize(v.has_value(), buffer)) { return false; }
        if(v.has_value()) { return serializer<T, SizeT>::serialize(*v, buffer); }
        return true;
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::optional<T>& v,
                                                   Buffer&           buffer) {
        bool hasValue{};
        if(!serializer<bool, SizeT>::deserialize(hasValue, buffer)) { return false; }
        if(hasValue) {
            v.emplace();
            return serializer<T, SizeT>::deserialize(*v, buffer);
        }
        v = std::nullopt;
        return true;
    }
};

template<typename T, typename E, typename SizeT>
struct serializer<std::expected<T, E>, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::expected<T,
                                                               E> const& v,
                                                 Buffer&                 buffer) {
        if(!serializer<bool, SizeT>::serialize(v.has_value(), buffer)) { return false; }
        if(v.has_value()) {
            if constexpr(!std::is_void_v<T>) { return serializer<T, SizeT>::serialize(*v, buffer); }
            return true;
        }
        return serializer<E, SizeT>::serialize(v.error(), buffer);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::expected<T,
                                                                 E>& v,
                                                   Buffer&           buffer) {
        bool hasValue{};
        if(!serializer<bool, SizeT>::deserialize(hasValue, buffer)) { return false; }
        if(hasValue) {
            v.emplace();
            if constexpr(!std::is_void_v<T>) {
                return serializer<T, SizeT>::deserialize(*v, buffer);
            }
            return true;
        }
        E err;
        if(!serializer<E, SizeT>::deserialize(err, buffer)) { return false; }
        v = std::unexpected{err};
        return true;
    }
};

template<typename... Ts, typename SizeT>
struct serializer<std::variant<Ts...>, SizeT> {
    // One alternative's (de)serialization, out of line and per alternative *type*: inlined
    // into the variant's dispatch, every alternative carried a copy of its type's code, and
    // alternatives of the same type (many commands share a request or a response type) carried
    // the same code again and again. The dispatch stays a switch on the index.
    template<typename T,
             typename Buffer>
    [[gnu::noinline]] static constexpr bool serializeAlternative(T const& alt,
                                                                 Buffer&  buffer) {
        return serializer<T, SizeT>::serialize(alt, buffer);
    }

    template<typename T,
             typename Buffer>
    [[gnu::noinline]] static constexpr bool deserializeAlternative(T&      alt,
                                                                   Buffer& buffer) {
        return serializer<T, SizeT>::deserialize(alt, buffer);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::variant<Ts...> const& v,
                                                 Buffer&                    buffer) {
        constexpr std::size_t N{sizeof...(Ts)};
        using Index_t
          = std::conditional_t<(N > std::numeric_limits<std::uint8_t>::max()), SizeT, std::uint8_t>;
        static_assert(std::numeric_limits<Index_t>::max() >= N, "variant too big");
        Index_t const index = static_cast<Index_t>(v.index());
        if(!serializer<Index_t, SizeT>::serialize(index, buffer)) { return false; }
        return std::visit([&](auto const& vv) { return serializeAlternative(vv, buffer); }, v);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::variant<Ts...>& v,
                                                   Buffer&              buffer) {
        constexpr std::size_t N{sizeof...(Ts)};
        using Index_t
          = std::conditional_t<(N > std::numeric_limits<std::uint8_t>::max()), SizeT, std::uint8_t>;
        static_assert(std::numeric_limits<Index_t>::max() >= N, "variant too big");
        Index_t index{};

        if(!serializer<Index_t, SizeT>::deserialize(index, buffer)) { return false; }
        if(index >= N) { return false; }

        auto action = [&](auto i) {
            static constexpr std::size_t I = decltype(i)::value;
            if(I == index) {
                // emplace() returns the alternative: no checked std::get<I> after it.
                auto& alt = v.template emplace<I>();
                return deserializeAlternative(alt, buffer);
            }
            return N > I;
        };

        return [&]<std::size_t... Ns>(std::index_sequence<Ns...>) {
            return (action(std::integral_constant<std::size_t, Ns>{}) && ...);
        }(std::make_index_sequence<N>{});
    }
};

template<typename Rep, typename Period, typename SizeT>
struct serializer<std::chrono::duration<Rep, Period>, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::chrono::duration<Rep,
                                                                       Period> const& v,
                                                 Buffer&                              buffer) {
        return serializer<Rep, SizeT>::serialize(v.count(), buffer);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::chrono::duration<Rep,
                                                                         Period>& v,
                                                   Buffer&                        buffer) {
        Rep vv;
        if(!serializer<Rep, SizeT>::deserialize(vv, buffer)) { return false; }
        v = std::chrono::duration<Rep, Period>{vv};
        return true;
    }
};

template<detail::IsTupleLikeButNotRange T, typename SizeT>
struct serializer<T, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(T const& v,
                                                 Buffer&  buffer) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (serializer<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>::serialize(
                      get<Is>(v),
                      buffer)
                    && ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(T&      v,
                                                   Buffer& buffer) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (
              serializer<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>::deserialize(
                get<Is>(v),
                buffer)
              && ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }
};

template<std::ranges::range T, typename SizeT>
struct serializer<T, SizeT> {
    static constexpr bool is_contiguous = std::ranges::contiguous_range<T>;
    using value_t                       = std::ranges::range_value_t<T>;
    static constexpr bool is_trivial    = detail::Trivial<value_t>;

    template<typename Buffer>
    static constexpr bool serialize(T const& v,
                                    Buffer&  buffer) {
        auto const fullSize = std::ranges::size(v);
        // Check for overflow when Size_t is smaller than the range's size type
        if constexpr(std::numeric_limits<SizeT>::max()
                     < std::numeric_limits<decltype(fullSize)>::max())
        {
            if(fullSize > std::numeric_limits<SizeT>::max()) {
                return false;   // Size exceeds Size_t capacity
            }
        }
        auto const size = static_cast<SizeT>(fullSize);

        if(!serializer<SizeT, SizeT>::serialize(size, buffer)) { return false; }

        if constexpr(is_contiguous && is_trivial && !detail::IsMap<T> && !detail::IsSet<T>) {
            return buffer.insert(std::as_bytes(std::span{v}));
        } else {
            for(auto const& vv : v) {
                if(!serializer<value_t, SizeT>::serialize(vv, buffer)) { return false; }
            }
            return true;
        }
    }

    template<typename Buffer>
    static constexpr bool deserialize(T&      v,
                                      Buffer& buffer) {
        SizeT size{};
        if(!serializer<SizeT, SizeT>::deserialize(size, buffer)) { return false; }
        if(size > buffer.size()) { return false; }

        if constexpr(requires { v.max_size(); }) {
            if(static_cast<std::size_t>(size) > v.max_size()) { return false; }
        }

        if constexpr(requires { v.resize(size); }) { v.resize(size); }

        if constexpr(detail::IsMap<T> || detail::IsSet<T>) {
            v.clear();
        } else {
            if(std::ranges::size(v) != size) { return false; }
        }

        if constexpr(is_contiguous && is_trivial && !detail::IsMap<T> && !detail::IsSet<T>) {
            return buffer.extract(std::as_writable_bytes(std::span{v}));
        } else {
            if constexpr(detail::IsMap<T> || detail::IsSet<T>) {
                while(size != 0) {
                    --size;
                    using value_type = typename detail::associative_container_value_type<T>::type;

                    value_type vv;
                    if(!serializer<value_type, SizeT>::deserialize(vv, buffer)) { return false; }
                    v.insert(vv);
                }
            } else {
                for(auto&& vv : v) {
                    if constexpr(requires { serializer<value_t, SizeT>::deserialize(vv, buffer); })
                    {
                        if(!serializer<value_t, SizeT>::deserialize(vv, buffer)) { return false; }
                    } else {
                        // Proxy reference, as std::vector<bool> hands out: assign through it.
                        value_t element{};
                        if(!serializer<value_t, SizeT>::deserialize(element, buffer)) {
                            return false;
                        }
                        vv = element;
                    }
                }
            }
            return true;
        }
    }
};

template<typename SizeT>
struct Serializer {
    template<typename Buffer,
             typename... Ts>
    static constexpr bool serialize(Buffer& buffer,
                                    Ts const&... vs) {
        return (serializer<std::remove_cvref_t<Ts>, SizeT>::serialize(vs, buffer) && ...);
    }

    template<typename Buffer,
             typename... Ts>
    static constexpr bool deserialize(Buffer& buffer,
                                      Ts&... vs) {
        return (serializer<std::remove_cvref_t<Ts>, SizeT>::deserialize(vs, buffer) && ...);
    }

    template<typename T,
             typename Buffer>
    static constexpr std::optional<T> deserialize(Buffer& buffer) {
        std::optional<T> v;
        v.emplace();
        if(!serializer<std::remove_cvref_t<T>, SizeT>::deserialize(*v, buffer)) {
            v = std::nullopt;
        }
        return v;
    }
};

template<detail::Trivial T, typename SizeT>
struct serialized_size<T, SizeT> : std::integral_constant<std::size_t, sizeof(T)> {};

template<typename Rep, typename Period, typename SizeT>
    requires HasFixedSerializedSize<Rep, SizeT>
struct serialized_size<std::chrono::duration<Rep, Period>, SizeT> : serialized_size<Rep, SizeT> {};

namespace detail {

    template<typename T,
             typename SizeT>
    consteval bool allTupleElementsHaveFixedSize() {
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            return (HasFixedSerializedSize<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>
                    && ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }

    template<typename T,
             typename SizeT>
    consteval std::size_t tupleSerializedSizeImpl() {
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            return (serialized_size<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>::value
                    + ... + std::size_t{0});
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }

    template<Described T,
             typename SizeT>
    consteval bool allMembersHaveFixedSize() {
        using TieType = decltype(glz::to_tie(std::declval<T&>()));
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (HasFixedSerializedSize<
                      std::remove_cvref_t<decltype(get<Is>(std::declval<TieType>()))>,
                      SizeT>
                    && ...);
        }(std::make_index_sequence<glz::reflect<T>::size>{});
    }

    template<Described T,
             typename SizeT>
    consteval std::size_t describedSerializedSizeImpl() {
        using TieType = decltype(glz::to_tie(std::declval<T&>()));
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (serialized_size<std::remove_cvref_t<decltype(get<Is>(std::declval<TieType>()))>,
                                    SizeT>::value
                    + ... + std::size_t{0});
        }(std::make_index_sequence<glz::reflect<T>::size>{});
    }

}   // namespace detail

template<detail::IsTupleLikeButNotRange T, typename SizeT>
    requires(detail::allTupleElementsHaveFixedSize<T, SizeT>())
struct serialized_size<T, SizeT>
  : std::integral_constant<std::size_t, detail::tupleSerializedSizeImpl<T, SizeT>()> {};

template<typename T, typename SizeT>
    requires(std::ranges::range<T> && detail::IsTupleLike<T>
             && HasFixedSerializedSize<std::ranges::range_value_t<T>, SizeT>)
struct serialized_size<T, SizeT>
  : std::integral_constant<std::size_t,
                           sizeof(SizeT)
                             + std::tuple_size_v<T>
                                 * serialized_size<std::ranges::range_value_t<T>, SizeT>::value> {};

template<Described T, typename SizeT>
    requires(detail::allMembersHaveFixedSize<T, SizeT>() && !detail::IsTupleLikeButNotRange<T>)
struct serialized_size<T, SizeT>
  : std::integral_constant<std::size_t, detail::describedSerializedSizeImpl<T, SizeT>()> {};

template<typename T, typename SizeT>
inline constexpr std::size_t serialized_size_v = serialized_size<T, SizeT>::value;

/// The most bytes serializer<T, Size_t> writes for any value of T - what a fixed-size frame has
/// to hold. It follows the choice of serializer above, case by case:
///  - a fixed serialized_size is its own bound;
///  - optional / expected: the flag and the larger side (expected<void, E>: flag + E);
///  - variant: the index (one byte, or Size_t past 255 alternatives) and the largest alternative;
///  - duration: its rep; tuple-likes and described structs: the sum of their members;
///  - a range: the Size_t count and capacity x its largest element, where the capacity is known
///    from the type alone - a static `capacity()` (a static vector, string, map or set) or a tuple
///    size (std::array). The count stops at what Size_t can hold: the serializer refuses more.
/// A std::vector, std::string or std::map has no such bound (a std::vector's capacity belongs to
/// the object), and neither has anything holding one: has_max_serialized_size is false for it.
/// A serializer specialisation whose wire format differs from the generic one states its bound as
/// a `static constexpr std::size_t max_serialized_size` member; without it the generic rule for
/// the type's shape applies, which is wrong for such a serializer.
/// The arithmetic saturates, so a huge capacity never wraps into a small "bound".
template<typename T, typename SizeT>
struct MaxSerializedSize;

namespace detail {

    /// The "no bound" value; every finite sum or product that reaches it saturates there.
    inline constexpr std::size_t unbounded_size = std::numeric_limits<std::size_t>::max();

    consteval std::size_t saturatingAdd(std::size_t a,
                                        std::size_t b) {
        return a > unbounded_size - b ? unbounded_size : a + b;
    }

    consteval std::size_t saturatingMul(std::size_t a,
                                        std::size_t b) {
        return a != 0 && b > unbounded_size / a ? unbounded_size : a * b;
    }

    template<typename... Ts>
    consteval std::size_t largestOf(Ts... vs) {
        std::size_t m{0};
        ((m = vs > m ? vs : m), ...);
        return m;
    }

    template<typename T>
    inline constexpr bool is_optional_v = false;
    template<typename T>
    inline constexpr bool is_optional_v<std::optional<T>> = true;

    template<typename T>
    inline constexpr bool is_expected_v = false;
    template<typename T, typename E>
    inline constexpr bool is_expected_v<std::expected<T, E>> = true;

    template<typename T>
    inline constexpr bool is_variant_v = false;
    template<typename... Ts>
    inline constexpr bool is_variant_v<std::variant<Ts...>> = true;

    template<typename T>
    inline constexpr bool is_duration_v = false;
    template<typename Rep, typename Period>
    inline constexpr bool is_duration_v<std::chrono::duration<Rep, Period>> = true;

    /// What the range serializer writes per element: a map's pair without the const key.
    template<typename T>
    struct RangeElement {
        using type = std::ranges::range_value_t<T>;
    };

    template<typename T>
        requires(IsMap<T> || IsSet<T>)
    struct RangeElement<T> {
        using type = typename associative_container_value_type<T>::type;
    };

    /// Elements a range can hold, known from its type: a static `capacity()` or a tuple size.
    template<typename T>
    consteval std::size_t staticCapacity() {
        if constexpr(requires { typename std::integral_constant<std::size_t, T::capacity()>; }) {
            return T::capacity();
        } else if constexpr(IsTupleLike<T>) {
            return std::tuple_size_v<T>;
        } else {
            return unbounded_size;
        }
    }

    template<typename T,
             typename SizeT>
    consteval std::size_t maxSizeOf();

    template<typename T,
             typename SizeT>
    consteval std::size_t tupleMaxSize() {
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            std::size_t sum{0};
            ((sum = saturatingAdd(
                sum,
                maxSizeOf<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>())),
             ...);
            return sum;
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }

    template<typename T,
             typename SizeT>
    consteval std::size_t describedMaxSize() {
        using TieType = decltype(glz::to_tie(std::declval<T&>()));
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            std::size_t sum{0};
            ((sum = saturatingAdd(
                sum,
                maxSizeOf<std::remove_cvref_t<decltype(get<Is>(std::declval<TieType>()))>,
                          SizeT>())),
             ...);
            return sum;
        }(std::make_index_sequence<glz::reflect<T>::size>{});
    }

    template<typename T,
             typename SizeT>
    consteval std::size_t rangeMaxSize() {
        constexpr std::size_t capacity = staticCapacity<T>();
        if constexpr(capacity == unbounded_size) {
            return unbounded_size;
        } else {
            constexpr std::size_t countLimit{std::numeric_limits<SizeT>::max()};
            constexpr std::size_t count = capacity < countLimit ? capacity : countLimit;
            return saturatingAdd(
              sizeof(SizeT),
              saturatingMul(count, maxSizeOf<typename RangeElement<T>::type, SizeT>()));
        }
    }

    template<typename T,
             typename SizeT>
    consteval std::size_t maxSizeOf() {
        if constexpr(requires {
                         {
                             serializer<T, SizeT>::max_serialized_size
                         } -> std::convertible_to<std::size_t>;
                     })
        {
            return serializer<T, SizeT>::max_serialized_size;
        } else if constexpr(HasFixedSerializedSize<T, SizeT>) {
            return serialized_size<T, SizeT>::value;
        } else if constexpr(is_optional_v<T>) {
            return saturatingAdd(1, maxSizeOf<typename T::value_type, SizeT>());
        } else if constexpr(is_expected_v<T>) {
            constexpr std::size_t value = [] {
                if constexpr(std::is_void_v<typename T::value_type>) {
                    return std::size_t{0};
                } else {
                    return maxSizeOf<typename T::value_type, SizeT>();
                }
            }();
            return saturatingAdd(1, largestOf(value, maxSizeOf<typename T::error_type, SizeT>()));
        } else if constexpr(is_variant_v<T>) {
            constexpr std::size_t N{std::variant_size_v<T>};
            // The same index type as serializer<std::variant>.
            using Index_t = std::
              conditional_t<(N > std::numeric_limits<std::uint8_t>::max()), SizeT, std::uint8_t>;
            return []<std::size_t... Is>(std::index_sequence<Is...>) {
                return saturatingAdd(
                  sizeof(Index_t),
                  largestOf(maxSizeOf<std::variant_alternative_t<Is, T>, SizeT>()...));
            }(std::make_index_sequence<N>{});
        } else if constexpr(is_duration_v<T>) {
            return maxSizeOf<typename T::rep, SizeT>();
        } else if constexpr(IsTupleLikeButNotRange<T>) {
            return tupleMaxSize<T, SizeT>();
        } else if constexpr(std::ranges::range<T>) {
            return rangeMaxSize<T, SizeT>();
        } else if constexpr(Described<T>) {
            return describedMaxSize<T, SizeT>();
        } else {
            return unbounded_size;
        }
    }

}   // namespace detail

template<typename T, typename SizeT>
struct MaxSerializedSize
  : std::integral_constant<std::size_t, detail::maxSizeOf<std::remove_cvref_t<T>, SizeT>()> {};

/// Every value of T serializes to at most max_serialized_size_v<T, Size_t> bytes.
template<typename T, typename SizeT>
concept HasMaxSerializedSize = MaxSerializedSize<T, SizeT>::value != detail::unbounded_size;

template<typename T, typename SizeT>
    requires HasMaxSerializedSize<T, SizeT>
inline constexpr std::size_t max_serialized_size_v = MaxSerializedSize<T, SizeT>::value;

}   // namespace aglio
