#pragma once

#include "inline.hpp"
#include "type_descriptor.hpp"

#include <algorithm>
#include <chrono>
#ifdef AGLIO_USE_MP_UNITS
    // macro-gated, not __has_include: a build that asks for quantities and lacks mp-units has to fail
    #include <mp-units/framework.h>
#endif
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
namespace Detail {

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
    struct RemovePairConst;

    template<typename K, typename V>
    struct RemovePairConst<std::pair<K, V>> {
        using type = std::pair<std::remove_const_t<K>, V>;
    };

    template<typename T>
    struct AssociativeContainerValueType;

    template<IsSet T>
    struct AssociativeContainerValueType<T> {
        using type = typename T::value_type;
    };

    template<IsMap T>
    struct AssociativeContainerValueType<T> {
        using type = typename RemovePairConst<typename T::value_type>::type;
    };

}   // namespace Detail

template<typename T, typename SizeT>
struct TypeSerializer;

template<typename T, typename SizeT>
struct SerializedSize;

template<typename T, typename SizeT>
concept HasFixedSerializedSize = requires {
    { SerializedSize<T, SizeT>::value } -> std::convertible_to<std::size_t>;
};

namespace Detail {

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
    constexpr bool isClaimedV = false;
    template<std::size_t N>
    constexpr bool isClaimedV<ClaimedForWrite<N>> = true;
    template<std::size_t N>
    constexpr bool isClaimedV<ClaimedForRead<N>> = true;

    /// A value of fixed, non-zero serialized size and a buffer with claim(n) that is not already
    /// a claim (nested fixed-size values go through the outer one).
    template<typename T, typename SizeT, typename Buffer>
    concept ClaimsWrite = HasFixedSerializedSize<T, SizeT> && (SerializedSize<T, SizeT>::value != 0)
                       && !isClaimedV<Buffer> && requires(Buffer& b) {
                              { b.claim(std::size_t{}) } -> std::same_as<std::byte*>;
                          };

    template<typename T, typename SizeT, typename Buffer>
    concept ClaimsRead = HasFixedSerializedSize<T, SizeT> && (SerializedSize<T, SizeT>::value != 0)
                      && !isClaimedV<Buffer> && requires(Buffer& b) {
                             { b.claim(std::size_t{}) } -> std::same_as<std::byte const*>;
                         };

}   // namespace Detail

template<typename SizeT>
struct TypeSerializer<bool, SizeT> {
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

template<Detail::Trivial T, typename SizeT>
struct TypeSerializer<T, SizeT> {
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
struct TypeSerializer<T, SizeT> {
    static_assert(glz::reflect<T>::size > 0 || std::is_empty_v<T>,
                  "Described type has members but glaze reflects 0. "
                  "This can happen with reference members. Provide a custom aglio::TypeSerializer "
                  "specialization.");

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(T const& v,
                                                 Buffer&  buffer) {
        constexpr auto members = std::make_index_sequence<glz::reflect<T>::size>{};
        if constexpr(Detail::ClaimsWrite<T, SizeT, Buffer>) {
            constexpr std::size_t n  = SerializedSize<T, SizeT>::value;
            std::byte* const      to = buffer.claim(n);
            if(to == nullptr) { return false; }
            Detail::ClaimedForWrite<n> claimed{to};
            // A SerializedSize that is not what the serializers write is a failure, never a gap.
            return serializeMembers(glz::to_tie(v), claimed, members) && claimed.used == n;
        } else {
            return serializeMembers(glz::to_tie(v), buffer, members);
        }
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(T&      v,
                                                   Buffer& buffer) {
        constexpr auto members = std::make_index_sequence<glz::reflect<T>::size>{};
        if constexpr(Detail::ClaimsRead<T, SizeT, Buffer>) {
            constexpr std::size_t  n    = SerializedSize<T, SizeT>::value;
            std::byte const* const from = buffer.claim(n);
            if(from == nullptr) { return false; }
            Detail::ClaimedForRead<n> claimed{from};
            return deserializeMembers(glz::to_tie(v), claimed, members) && claimed.used == n;
        } else {
            return deserializeMembers(glz::to_tie(v), buffer, members);
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
        return (TypeSerializer<std::remove_cvref_t<decltype(get<Is>(tie))>, SizeT>::serialize(
                  get<Is>(tie),
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
        return (TypeSerializer<std::remove_cvref_t<decltype(get<Is>(tie))>, SizeT>::deserialize(
                  get<Is>(tie),
                  buffer)
                && ...);
    }
};

template<typename T, typename SizeT>
struct TypeSerializer<std::optional<T>, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::optional<T> const& v,
                                                 Buffer&                 buffer) {
        if(!TypeSerializer<bool, SizeT>::serialize(v.has_value(), buffer)) { return false; }
        if(v.has_value()) { return TypeSerializer<T, SizeT>::serialize(*v, buffer); }
        return true;
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::optional<T>& v,
                                                   Buffer&           buffer) {
        bool hasValue{};
        if(!TypeSerializer<bool, SizeT>::deserialize(hasValue, buffer)) { return false; }
        if(hasValue) {
            v.emplace();
            return TypeSerializer<T, SizeT>::deserialize(*v, buffer);
        }
        v = std::nullopt;
        return true;
    }
};

template<typename T, typename E, typename SizeT>
struct TypeSerializer<std::expected<T, E>, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::expected<T,
                                                               E> const& v,
                                                 Buffer&                 buffer) {
        if(!TypeSerializer<bool, SizeT>::serialize(v.has_value(), buffer)) { return false; }
        if(v.has_value()) {
            if constexpr(!std::is_void_v<T>) {
                return TypeSerializer<T, SizeT>::serialize(*v, buffer);
            }
            return true;
        }
        return TypeSerializer<E, SizeT>::serialize(v.error(), buffer);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::expected<T,
                                                                 E>& v,
                                                   Buffer&           buffer) {
        bool hasValue{};
        if(!TypeSerializer<bool, SizeT>::deserialize(hasValue, buffer)) { return false; }
        if(hasValue) {
            v.emplace();
            if constexpr(!std::is_void_v<T>) {
                return TypeSerializer<T, SizeT>::deserialize(*v, buffer);
            }
            return true;
        }
        E err;
        if(!TypeSerializer<E, SizeT>::deserialize(err, buffer)) { return false; }
        v = std::unexpected{err};
        return true;
    }
};

template<typename... Ts, typename SizeT>
struct TypeSerializer<std::variant<Ts...>, SizeT> {
    // One alternative's (de)serialization, out of line and per alternative *type*: inlined
    // into the variant's dispatch, every alternative carried a copy of its type's code, and
    // alternatives of the same type (many commands share a request or a response type) carried
    // the same code again and again. The dispatch stays a switch on the index.
    template<typename T,
             typename Buffer>
    [[gnu::noinline]] static constexpr bool serializeAlternative(T const& alt,
                                                                 Buffer&  buffer) {
        return TypeSerializer<T, SizeT>::serialize(alt, buffer);
    }

    template<typename T,
             typename Buffer>
    [[gnu::noinline]] static constexpr bool deserializeAlternative(T&      alt,
                                                                   Buffer& buffer) {
        return TypeSerializer<T, SizeT>::deserialize(alt, buffer);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::variant<Ts...> const& v,
                                                 Buffer&                    buffer) {
        constexpr std::size_t n{sizeof...(Ts)};
        using Index_t
          = std::conditional_t<(n > std::numeric_limits<std::uint8_t>::max()), SizeT, std::uint8_t>;
        static_assert(std::numeric_limits<Index_t>::max() >= n, "variant too big");
        Index_t const index = static_cast<Index_t>(v.index());
        if(!TypeSerializer<Index_t, SizeT>::serialize(index, buffer)) { return false; }
        // a fold over the alternatives, not std::visit: each a direct call (libc++'s visit dispatches through a
        // table of function pointers, which a static call graph - Kvasir's stack bound - cannot follow)
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            bool ok = false;
            static_cast<void>(
              ((v.index() == I ? (ok = serializeAlternative(*std::get_if<I>(&v), buffer), true)
                               : false)
               || ...));
            return ok;
        }(std::index_sequence_for<Ts...>{});
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::variant<Ts...>& v,
                                                   Buffer&              buffer) {
        constexpr std::size_t n{sizeof...(Ts)};
        using Index_t
          = std::conditional_t<(n > std::numeric_limits<std::uint8_t>::max()), SizeT, std::uint8_t>;
        static_assert(std::numeric_limits<Index_t>::max() >= n, "variant too big");
        Index_t index{};

        if(!TypeSerializer<Index_t, SizeT>::deserialize(index, buffer)) { return false; }
        if(index >= n) { return false; }

        auto action = [&](auto i) {
            static constexpr std::size_t idx = decltype(i)::value;
            if(idx == index) {
                // emplace() returns the alternative: no checked std::get<I> after it.
                auto& alt = v.template emplace<idx>();
                return deserializeAlternative(alt, buffer);
            }
            return n > idx;
        };

        return [&]<std::size_t... Ns>(std::index_sequence<Ns...>) {
            return (action(std::integral_constant<std::size_t, Ns>{}) && ...);
        }(std::make_index_sequence<n>{});
    }
};

template<typename Rep, typename Period, typename SizeT>
struct TypeSerializer<std::chrono::duration<Rep, Period>, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(std::chrono::duration<Rep,
                                                                       Period> const& v,
                                                 Buffer&                              buffer) {
        return TypeSerializer<Rep, SizeT>::serialize(v.count(), buffer);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(std::chrono::duration<Rep,
                                                                         Period>& v,
                                                   Buffer&                        buffer) {
        Rep vv;
        if(!TypeSerializer<Rep, SizeT>::deserialize(vv, buffer)) { return false; }
        v = std::chrono::duration<Rep, Period>{vv};
        return true;
    }
};

#ifdef AGLIO_USE_MP_UNITS
/// An mp-units quantity travels as its number in its own unit, as a duration as its count: the unit is in the type
/// at both ends (a receiver naming another unit reads another number - name the same type). Built back with the
/// (number, reference) constructor, which takes offset units (degree_Celsius) as well.
template<auto R, typename Rep, typename SizeT>
struct TypeSerializer<mp_units::quantity<R, Rep>, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(mp_units::quantity<R,
                                                                    Rep> const& v,
                                                 Buffer&                        buffer) {
        return TypeSerializer<Rep, SizeT>::serialize(v.numerical_value_in(v.unit), buffer);
    }

    template<typename Buffer>
    AGLIO_INLINE static constexpr bool deserialize(mp_units::quantity<R,
                                                                      Rep>& v,
                                                   Buffer&                  buffer) {
        Rep vv;
        if(!TypeSerializer<Rep, SizeT>::deserialize(vv, buffer)) { return false; }
        v = mp_units::quantity<R, Rep>{vv, R};
        return true;
    }
};
#endif

template<Detail::IsTupleLikeButNotRange T, typename SizeT>
struct TypeSerializer<T, SizeT> {
    template<typename Buffer>
    AGLIO_INLINE static constexpr bool serialize(T const& v,
                                                 Buffer&  buffer) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (
              TypeSerializer<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>::serialize(
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
              TypeSerializer<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>::deserialize(
                get<Is>(v),
                buffer)
              && ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }
};

template<std::ranges::range T, typename SizeT>
struct TypeSerializer<T, SizeT> {
    static constexpr bool isContiguous = std::ranges::contiguous_range<T>;
    using value_t                      = std::ranges::range_value_t<T>;
    static constexpr bool isTrivial    = Detail::Trivial<value_t>;

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

        if(!TypeSerializer<SizeT, SizeT>::serialize(size, buffer)) { return false; }

        if constexpr(isContiguous && isTrivial && !Detail::IsMap<T> && !Detail::IsSet<T>) {
            return buffer.insert(std::as_bytes(std::span{v}));
        } else {
            for(auto const& vv : v) {
                if(!TypeSerializer<value_t, SizeT>::serialize(vv, buffer)) { return false; }
            }
            return true;
        }
    }

    template<typename Buffer>
    static constexpr bool deserialize(T&      v,
                                      Buffer& buffer) {
        SizeT size{};
        if(!TypeSerializer<SizeT, SizeT>::deserialize(size, buffer)) { return false; }
        if(size > buffer.size()) { return false; }

        if constexpr(requires { v.max_size(); }) {
            if(static_cast<std::size_t>(size) > v.max_size()) { return false; }
        }

        if constexpr(requires { v.resize(size); }) { v.resize(size); }

        if constexpr(Detail::IsMap<T> || Detail::IsSet<T>) {
            v.clear();
        } else {
            if(std::ranges::size(v) != size) { return false; }
        }

        if constexpr(isContiguous && isTrivial && !Detail::IsMap<T> && !Detail::IsSet<T>) {
            return buffer.extract(std::as_writable_bytes(std::span{v}));
        } else {
            if constexpr(Detail::IsMap<T> || Detail::IsSet<T>) {
                while(size != 0) {
                    --size;
                    using value_type = typename Detail::AssociativeContainerValueType<T>::type;

                    value_type vv;
                    if(!TypeSerializer<value_type, SizeT>::deserialize(vv, buffer)) {
                        return false;
                    }
                    v.insert(vv);
                }
            } else {
                for(auto&& vv : v) {
                    if constexpr(requires {
                                     TypeSerializer<value_t, SizeT>::deserialize(vv, buffer);
                                 })
                    {
                        if(!TypeSerializer<value_t, SizeT>::deserialize(vv, buffer)) {
                            return false;
                        }
                    } else {
                        // Proxy reference, as std::vector<bool> hands out: assign through it.
                        value_t element{};
                        if(!TypeSerializer<value_t, SizeT>::deserialize(element, buffer)) {
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
        return (TypeSerializer<std::remove_cvref_t<Ts>, SizeT>::serialize(vs, buffer) && ...);
    }

    template<typename Buffer,
             typename... Ts>
    static constexpr bool deserialize(Buffer& buffer,
                                      Ts&... vs) {
        return (TypeSerializer<std::remove_cvref_t<Ts>, SizeT>::deserialize(vs, buffer) && ...);
    }

    template<typename T,
             typename Buffer>
    static constexpr std::optional<T> deserialize(Buffer& buffer) {
        std::optional<T> v;
        v.emplace();
        if(!TypeSerializer<std::remove_cvref_t<T>, SizeT>::deserialize(*v, buffer)) {
            v = std::nullopt;
        }
        return v;
    }
};

template<Detail::Trivial T, typename SizeT>
struct SerializedSize<T, SizeT> : std::integral_constant<std::size_t, sizeof(T)> {};

template<typename Rep, typename Period, typename SizeT>
    requires HasFixedSerializedSize<Rep, SizeT>
struct SerializedSize<std::chrono::duration<Rep, Period>, SizeT> : SerializedSize<Rep, SizeT> {};

#ifdef AGLIO_USE_MP_UNITS
template<auto R, typename Rep, typename SizeT>
    requires HasFixedSerializedSize<Rep, SizeT>
struct SerializedSize<mp_units::quantity<R, Rep>, SizeT> : SerializedSize<Rep, SizeT> {};
#endif

namespace Detail {

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
            return (SerializedSize<std::remove_cvref_t<std::tuple_element_t<Is, T>>, SizeT>::value
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
            return (SerializedSize<std::remove_cvref_t<decltype(get<Is>(std::declval<TieType>()))>,
                                   SizeT>::value
                    + ... + std::size_t{0});
        }(std::make_index_sequence<glz::reflect<T>::size>{});
    }

}   // namespace Detail

template<Detail::IsTupleLikeButNotRange T, typename SizeT>
    requires(Detail::allTupleElementsHaveFixedSize<T, SizeT>())
struct SerializedSize<T, SizeT>
  : std::integral_constant<std::size_t, Detail::tupleSerializedSizeImpl<T, SizeT>()> {};

template<typename T, typename SizeT>
    requires(std::ranges::range<T> && Detail::IsTupleLike<T>
             && HasFixedSerializedSize<std::ranges::range_value_t<T>, SizeT>)
struct SerializedSize<T, SizeT>
  : std::integral_constant<std::size_t,
                           sizeof(SizeT)
                             + std::tuple_size_v<T>
                                 * SerializedSize<std::ranges::range_value_t<T>, SizeT>::value> {};

template<Described T, typename SizeT>
    requires(Detail::allMembersHaveFixedSize<T, SizeT>() && !Detail::IsTupleLikeButNotRange<T>)
struct SerializedSize<T, SizeT>
  : std::integral_constant<std::size_t, Detail::describedSerializedSizeImpl<T, SizeT>()> {};

template<typename T, typename SizeT>
inline constexpr std::size_t serializedSizeV = SerializedSize<T, SizeT>::value;

/// The most bytes TypeSerializer<T, Size_t> writes for any value of T - what a fixed-size frame has
/// to hold. It follows the choice of serializer above, case by case:
///  - a fixed SerializedSize is its own bound;
///  - optional / expected: the flag and the larger side (expected<void, E>: flag + E);
///  - variant: the index (one byte, or Size_t past 255 alternatives) and the largest alternative;
///  - duration and (AGLIO_USE_MP_UNITS) mp-units quantity: its rep; tuple-likes and described structs: the sum of
///    their members;
///  - a range: the Size_t count and capacity x its largest element, where the capacity is known
///    from the type alone - a static `capacity()` (a static vector, string, map or set) or a tuple
///    size (std::array). The count stops at what Size_t can hold: the serializer refuses more.
/// A std::vector, std::string or std::map has no such bound (a std::vector's capacity belongs to
/// the object), and neither has anything holding one: HasMaxSerializedSize is false for it.
/// A TypeSerializer specialisation whose wire format differs from the generic one states its bound as
/// a `static constexpr std::size_t maxSerializedSize` member; without it the generic rule for
/// the type's shape applies, which is wrong for such a serializer.
/// The arithmetic saturates, so a huge capacity never wraps into a small "bound".
template<typename T, typename SizeT>
struct MaxSerializedSize;

namespace Detail {

    /// The "no bound" value; every finite sum or product that reaches it saturates there.
    inline constexpr std::size_t unboundedSize = std::numeric_limits<std::size_t>::max();

    consteval std::size_t saturatingAdd(std::size_t a,
                                        std::size_t b) {
        return a > unboundedSize - b ? unboundedSize : a + b;
    }

    consteval std::size_t saturatingMul(std::size_t a,
                                        std::size_t b) {
        return a != 0 && b > unboundedSize / a ? unboundedSize : a * b;
    }

    template<typename... Ts>
    consteval std::size_t largestOf(Ts... vs) {
        std::size_t m{0};
        ((m = vs > m ? vs : m), ...);
        return m;
    }

    template<typename T>
    inline constexpr bool isOptionalV = false;
    template<typename T>
    inline constexpr bool isOptionalV<std::optional<T>> = true;

    template<typename T>
    inline constexpr bool isExpectedV = false;
    template<typename T, typename E>
    inline constexpr bool isExpectedV<std::expected<T, E>> = true;

    template<typename T>
    inline constexpr bool isVariantV = false;
    template<typename... Ts>
    inline constexpr bool isVariantV<std::variant<Ts...>> = true;

    template<typename T>
    inline constexpr bool isDurationV = false;
    template<typename Rep, typename Period>
    inline constexpr bool isDurationV<std::chrono::duration<Rep, Period>> = true;

    template<typename T>
    inline constexpr bool isQuantityV = false;
#ifdef AGLIO_USE_MP_UNITS
    template<auto R, typename Rep>
    inline constexpr bool isQuantityV<mp_units::quantity<R, Rep>> = true;
#endif

    /// What the range serializer writes per element: a map's pair without the const key.
    template<typename T>
    struct RangeElement {
        using type = std::ranges::range_value_t<T>;
    };

    template<typename T>
        requires(IsMap<T> || IsSet<T>)
    struct RangeElement<T> {
        using type = typename AssociativeContainerValueType<T>::type;
    };

    /// Elements a range can hold, known from its type: a static `capacity()` or a tuple size.
    template<typename T>
    consteval std::size_t staticCapacity() {
        if constexpr(requires { typename std::integral_constant<std::size_t, T::capacity()>; }) {
            return T::capacity();
        } else if constexpr(IsTupleLike<T>) {
            return std::tuple_size_v<T>;
        } else {
            return unboundedSize;
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
        if constexpr(capacity == unboundedSize) {
            return unboundedSize;
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
                             TypeSerializer<T, SizeT>::maxSerializedSize
                         } -> std::convertible_to<std::size_t>;
                     })
        {
            return TypeSerializer<T, SizeT>::maxSerializedSize;
        } else if constexpr(HasFixedSerializedSize<T, SizeT>) {
            return SerializedSize<T, SizeT>::value;
        } else if constexpr(isOptionalV<T>) {
            return saturatingAdd(1, maxSizeOf<typename T::value_type, SizeT>());
        } else if constexpr(isExpectedV<T>) {
            constexpr std::size_t value = [] {
                if constexpr(std::is_void_v<typename T::value_type>) {
                    return std::size_t{0};
                } else {
                    return maxSizeOf<typename T::value_type, SizeT>();
                }
            }();
            return saturatingAdd(1, largestOf(value, maxSizeOf<typename T::error_type, SizeT>()));
        } else if constexpr(isVariantV<T>) {
            constexpr std::size_t n{std::variant_size_v<T>};
            // The same index type as TypeSerializer<std::variant>.
            using Index_t = std::
              conditional_t<(n > std::numeric_limits<std::uint8_t>::max()), SizeT, std::uint8_t>;
            return []<std::size_t... Is>(std::index_sequence<Is...>) {
                return saturatingAdd(
                  sizeof(Index_t),
                  largestOf(maxSizeOf<std::variant_alternative_t<Is, T>, SizeT>()...));
            }(std::make_index_sequence<n>{});
        } else if constexpr(isDurationV<T> || isQuantityV<T>) {
            return maxSizeOf<typename T::rep, SizeT>();
        } else if constexpr(IsTupleLikeButNotRange<T>) {
            return tupleMaxSize<T, SizeT>();
        } else if constexpr(std::ranges::range<T>) {
            return rangeMaxSize<T, SizeT>();
        } else if constexpr(Described<T>) {
            return describedMaxSize<T, SizeT>();
        } else {
            return unboundedSize;
        }
    }

}   // namespace Detail

template<typename T, typename SizeT>
struct MaxSerializedSize
  : std::integral_constant<std::size_t, Detail::maxSizeOf<std::remove_cvref_t<T>, SizeT>()> {};

/// Every value of T serializes to at most maxSerializedSizeV<T, Size_t> bytes.
template<typename T, typename SizeT>
concept HasMaxSerializedSize = MaxSerializedSize<T, SizeT>::value != Detail::unboundedSize;

template<typename T, typename SizeT>
    requires HasMaxSerializedSize<T, SizeT>
inline constexpr std::size_t maxSerializedSizeV = MaxSerializedSize<T, SizeT>::value;

}   // namespace aglio
