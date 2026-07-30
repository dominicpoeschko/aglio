#pragma once

#include "packager_configs.hpp"
#include "types.hpp"

#include <aglio/packager.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

// The (config, message type) matrix the fuzz target runs. Shared with the corpus generator so seeds
// carry the right selector byte; if the two disagreed every seed would decode as garbage.
namespace Test::fuzz {

// ValueEq adds value equality on top of the byte oracle. Off for types with floating point members:
// a fuzzed pattern can be a NaN, which never equals itself, so it would fire on a good round trip.
template<typename Config_, typename Type_, bool ValueEq_ = true>
struct Case {
    using Config                  = Config_;
    using Type                    = Type_;
    static constexpr bool ValueEq = ValueEq_;
};

using Cases = std::tuple<Case<packager::Configs::Minimal, Types::Container>,
                         Case<packager::Configs::SimplePackageStart, Types::Container>,
                         Case<packager::Configs::SimpleCrc, Types::Nested>,
                         Case<packager::Configs::CrcNoHeader, Types::Associative>,
                         Case<packager::Configs::Full, Types::Wrapper, false>,
                         Case<packager::Configs::FullNoHeaderCrc, Types::Primitive, false>,
                         Case<packager::Configs::WithHeaderData, Types::Container>,
                         Case<packager::Configs::WithDescribedHeaderData, Types::Nested>,
                         Case<packager::Configs::WithDescribedCrc, Types::Chrono>,
                         Case<packager::Configs::SmallMax, Types::Empty>,
                         Case<packager::Configs::NarrowSize, Types::Associative>,
                         Case<packager::Configs::Minimal, Types::ContiguousAssociative>,
                         Case<packager::Configs::TinySize, Types::Container>,
                         // Append, never insert: selector bytes are taken modulo CaseCount, so
                         // reordering remaps every committed corpus input.
                         Case<packager::Configs::WithDescribedCrc, Types::WrapperNoFloat>,
                         // deque, list, set<string> and array<string> each take their own path
                         // through the range serializer.
                         Case<packager::Configs::SimpleCrc, Types::MoreContainers>,
                         Case<packager::Configs::Full, Types::NestedWrappers>>;

inline constexpr std::size_t CaseCount{std::tuple_size_v<Cases>};

// Invokes f with a Case tag, which picks the packager instantiation at compile time.
template<typename F>
constexpr void with_case(std::size_t index,
                         F&&         f) {
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        static_cast<void>(
          ((Is == index ? (f(std::tuple_element_t<Is, Cases>{}), true) : false) || ...));
    }(std::make_index_sequence<CaseCount>{});
}

// Built from `value` so seeds get a fixed one and the mutator can vary it.
template<typename Config>
constexpr auto header_data_seed(unsigned int value = 1) {
    typename Config::HeaderData info{};
    if constexpr(std::is_integral_v<typename Config::HeaderData>) {
        info = static_cast<decltype(info)>(value & 0xFFu);
    } else if constexpr(requires { info.msg_type = 1; }) {
        info.msg_type = static_cast<std::uint8_t>(value & 0xFFu);
        info.channel  = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    }
    return info;
}

// A body pack() copies verbatim, so the mutator can wrap arbitrary bytes in a real header without
// duplicating the frame layout.
struct RawBody {
    std::span<std::byte const> bytes{};
};

// pack() takes a third argument exactly for the HeaderData configs.
template<typename Case,
         typename Buffer,
         typename T>
bool pack_case(Buffer&      buffer,
               T const&     value,
               unsigned int headerValue = 1) {
    using Packager = aglio::Packager<typename Case::Config>;
    if constexpr(requires { typename Case::Config::HeaderData; }) {
        return Packager::pack(buffer, value, header_data_seed<typename Case::Config>(headerValue));
    } else {
        return Packager::pack(buffer, value);
    }
}

}   // namespace Test::fuzz

namespace aglio {
template<typename Size_t>
struct serializer<Test::fuzz::RawBody, Size_t> {
    template<typename Buffer>
    static bool serialize(Test::fuzz::RawBody const& v,
                          Buffer&                    buffer) {
        return buffer.insert(v.bytes);
    }
};
}   // namespace aglio
