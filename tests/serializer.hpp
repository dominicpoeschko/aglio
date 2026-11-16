#pragma once

#include "types.hpp"

#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <ratio>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#ifdef AGLIO_USE_MP_UNITS
    #include <mp-units/systems/si.h>
#endif

namespace Test::TypeSerializer {

template<typename SizeT,
         typename T>
bool roundTrip(T const& in,
               T&       out) {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    if(!aglio::TypeSerializer<T, SizeT>::serialize(in, ser)) { return false; }
    aglio::DynamicDeserializationView de{buffer};
    return aglio::TypeSerializer<T, SizeT>::deserialize(out, de);
}

template<typename SizeT,
         typename T>
bool roundTripCheck(T const& in) {
    T out{};
    if(!roundTrip<SizeT>(in, out)) { return false; }
    return in == out;
}

/// The same for a type whose value-initialised state is no value of it (an enum without a zero
/// enumerator): `out` starts as given.
template<typename SizeT,
         typename T>
bool roundTripCheckFrom(T const& in,
                        T        out) {
    if(!roundTrip<SizeT>(in, out)) { return false; }
    return in == out;
}

struct TwoFieldAggregate {
    std::uint8_t a{};
    std::uint8_t b{};
    bool         operator==(TwoFieldAggregate const&) const = default;
};

}   // namespace Test::TypeSerializer

TEST_CASE("Serializer: empty containers round-trip",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S = std::uint32_t;

    CHECK(roundTripCheck<S>(std::vector<int>{}));
    CHECK(roundTripCheck<S>(std::map<int, int>{}));
    CHECK(roundTripCheck<S>(std::set<int>{}));
    CHECK(roundTripCheck<S>(std::string{}));
}

TEST_CASE("Serializer: range size overflow with small Size_t",
          "[serializer]") {
    std::vector<std::uint8_t> const big(300, 0x42);

    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    bool const                      ok
      = aglio::TypeSerializer<std::vector<std::uint8_t>, std::uint8_t>::serialize(big, ser);
    CHECK(!ok);
}

TEST_CASE("Serializer: variant out-of-bounds index",
          "[serializer]") {
    using V = std::variant<int, float>;
    using S = std::uint32_t;

    V const                         vIn = 42;
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::TypeSerializer<V, S>::serialize(vIn, ser));

    buffer[0] = std::byte{5};

    V                                 vOut;
    aglio::DynamicDeserializationView de{buffer};
    CHECK(!aglio::TypeSerializer<V, S>::deserialize(vOut, de));
}

TEST_CASE("Serializer: deserialize from truncated buffer",
          "[serializer]") {
    using S = std::uint32_t;

    Types::Primitive const          pIn = Types::createDefault<Types::Primitive>();
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::TypeSerializer<Types::Primitive, S>::serialize(pIn, ser));

    buffer.resize(buffer.size() / 2);

    Types::Primitive                  pOut{};
    aglio::DynamicDeserializationView de{buffer};
    CHECK(!aglio::TypeSerializer<Types::Primitive, S>::deserialize(pOut, de));
}

TEST_CASE("Serializer: optional round-trip both states",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S = std::uint32_t;

    CHECK(roundTripCheck<S>(std::optional<int>{42}));
    CHECK(roundTripCheck<S>(std::optional<int>{std::nullopt}));
}

TEST_CASE("Serializer: expected round-trip both states",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S = std::uint32_t;
    using E = std::expected<int, std::string>;

    CHECK(roundTripCheck<S>(E{42}));
    CHECK(roundTripCheck<S>(E{std::unexpected{std::string("err")}}));
}

TEST_CASE("Serializer: variant round-trip each alternative",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S = std::uint32_t;
    using V = std::variant<int, std::string, float>;

    CHECK(roundTripCheck<S>(V{123}));
    CHECK(roundTripCheck<S>(V{std::string("hello")}));
    CHECK(roundTripCheck<S>(V{3.14f}));
}

TEST_CASE("Serializer: chrono duration round-trip",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S = std::uint32_t;

    CHECK(roundTripCheck<S>(std::chrono::milliseconds{12345}));
    CHECK(roundTripCheck<S>(std::chrono::nanoseconds{9876543210LL}));
}

TEST_CASE("Serializer: a duration of a minute or longer travels as 32 bits on every machine",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using aglio::serializedSizeV;
    using S = std::uint16_t;

    // the standard's own names: nanoseconds to seconds count in 64 bits everywhere, the longer ones in
    // whatever the library chose - `long` or `int` or 64 bits - and all travel as 32
    static_assert(serializedSizeV<std::chrono::nanoseconds, S> == 8);
    static_assert(serializedSizeV<std::chrono::microseconds, S> == 8);
    static_assert(serializedSizeV<std::chrono::milliseconds, S> == 8);
    static_assert(serializedSizeV<std::chrono::seconds, S> == 8);
    static_assert(serializedSizeV<std::chrono::minutes, S> == 4);
    static_assert(serializedSizeV<std::chrono::hours, S> == 4);
    static_assert(serializedSizeV<std::chrono::days, S> == 4);
    static_assert(serializedSizeV<std::chrono::weeks, S> == 4);
    static_assert(serializedSizeV<std::chrono::months, S> == 4);
    static_assert(serializedSizeV<std::chrono::years, S> == 4);
    static_assert(aglio::maxSerializedSizeV<std::chrono::hours, S> == 4);

    // The standard asks each of its names for a signed count of at least so many bits, and promises
    // +-292 years up to hours and +-40000 years from days on. What travels keeps both.
    auto const wireBits = []<typename D>() {
        return std::numeric_limits<
                 aglio::Detail::DurationWireRep<typename D::rep, typename D::period>>::digits
             + 1;
    };
    static_assert(wireBits.template operator()<std::chrono::nanoseconds>() >= 64);
    static_assert(wireBits.template operator()<std::chrono::microseconds>() >= 55);
    static_assert(wireBits.template operator()<std::chrono::milliseconds>() >= 45);
    static_assert(wireBits.template operator()<std::chrono::seconds>() >= 35);
    static_assert(wireBits.template operator()<std::chrono::minutes>() >= 29);
    static_assert(wireBits.template operator()<std::chrono::hours>() >= 23);
    static_assert(wireBits.template operator()<std::chrono::days>() >= 25);
    static_assert(wireBits.template operator()<std::chrono::weeks>() >= 22);
    static_assert(wireBits.template operator()<std::chrono::months>() >= 20);
    static_assert(wireBits.template operator()<std::chrono::years>() >= 17);
    constexpr std::int64_t most = std::numeric_limits<std::int32_t>::max();
    using Years64               = std::chrono::duration<std::int64_t, std::chrono::years::period>;
    static_assert(std::chrono::duration<std::int64_t, std::ratio<60>>{most} >= Years64{292});
    static_assert(std::chrono::duration<std::int64_t, std::ratio<3600>>{most} >= Years64{292});
    static_assert(std::chrono::duration<std::int64_t, std::ratio<86400>>{most} >= Years64{40000});
    static_assert(std::chrono::duration<std::int64_t, std::chrono::months::period>{most}
                  >= Years64{40000});

    // whatever counts them in memory, when it is a signed integer of 32 bits or more
    using Hours64 = std::chrono::duration<std::int64_t, std::ratio<3600>>;
    using Hours32 = std::chrono::duration<std::int32_t, std::ratio<3600>>;
    static_assert(serializedSizeV<Hours64, S> == 4);
    static_assert(serializedSizeV<Hours32, S> == 4);
    static_assert(serializedSizeV<std::chrono::duration<long, std::ratio<60>>, S> == 4);
    static_assert(serializedSizeV<std::chrono::duration<long long, std::ratio<60>>, S> == 4);

    // a count somebody named otherwise is theirs, and so is everything shorter than a minute
    static_assert(serializedSizeV<std::chrono::duration<std::int16_t, std::ratio<3600>>, S> == 2);
    static_assert(serializedSizeV<std::chrono::duration<std::uint64_t, std::ratio<3600>>, S> == 8);
    static_assert(serializedSizeV<std::chrono::duration<double, std::ratio<3600>>, S> == 8);
    static_assert(serializedSizeV<std::chrono::duration<float, std::ratio<60>>, S> == 4);
    static_assert(serializedSizeV<std::chrono::duration<std::int64_t, std::ratio<59>>, S> == 8);
    static_assert(serializedSizeV<std::chrono::duration<std::int32_t, std::milli>, S> == 4);
    static_assert(serializedSizeV<std::chrono::duration<std::int64_t, std::milli>, S> == 8);

    CHECK(roundTripCheck<S>(std::chrono::minutes{90}));
    CHECK(roundTripCheck<S>(std::chrono::hours{-48}));
    CHECK(roundTripCheck<S>(std::chrono::days{365}));
    CHECK(roundTripCheck<S>(std::chrono::years{-2000}));
    CHECK(roundTripCheck<S>(Hours64{std::numeric_limits<std::int32_t>::max()}));
    CHECK(roundTripCheck<S>(Hours64{std::numeric_limits<std::int32_t>::min()}));

    // the bytes: the count's low 32 bits, sign and all - what a 32-bit machine's `long` is
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::TypeSerializer<Hours64, S>::serialize(Hours64{-2}, ser));
    REQUIRE(buffer.size() == 4);
    CHECK(buffer[0] == std::byte{0xFE});
    CHECK(buffer[3] == std::byte{0xFF});

    // and they read back into a wider count with their sign
    Hours64                           wide{};
    aglio::DynamicDeserializationView de{buffer};
    REQUIRE(aglio::TypeSerializer<Hours64, S>::deserialize(wide, de));
    CHECK(wide.count() == -2);

    // a count 32 bits cannot hold is refused, not cut - and nothing is written
    std::vector<std::byte>          none;
    aglio::DynamicSerializationView noneSer{none};
    CHECK(!aglio::TypeSerializer<Hours64, S>::serialize(Hours64{std::int64_t{1} << 32}, noneSer));
    CHECK(!aglio::TypeSerializer<Hours64, S>::serialize(
      Hours64{std::int64_t{std::numeric_limits<std::int32_t>::min()} - 1},
      noneSer));
    CHECK(none.empty());
}

TEST_CASE("Serializer: nested containers with mixed optional",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S = std::uint32_t;

    std::vector<std::optional<int>> const v{1, std::nullopt, 3, std::nullopt, 5};
    CHECK(roundTripCheck<S>(v));
}

TEST_CASE("serializedSizeV compile-time values",
          "[serializer][SerializedSize]") {
    using aglio::serializedSizeV;

    static_assert(serializedSizeV<std::uint8_t, std::uint32_t> == 1);
    static_assert(serializedSizeV<std::uint16_t, std::uint32_t> == 2);
    static_assert(serializedSizeV<std::uint32_t, std::uint32_t> == 4);
    static_assert(serializedSizeV<std::int64_t, std::uint32_t> == 8);
    static_assert(serializedSizeV<float, std::uint32_t> == 4);
    static_assert(serializedSizeV<double, std::uint32_t> == 8);
    static_assert(serializedSizeV<bool, std::uint32_t> == 1);

    static_assert(serializedSizeV<Types::Color, std::uint32_t> == sizeof(std::uint8_t));
    static_assert(serializedSizeV<Types::Status, std::uint32_t> == sizeof(int));

    static_assert(serializedSizeV<std::chrono::nanoseconds, std::uint32_t> == sizeof(std::int64_t));
    static_assert(serializedSizeV<std::chrono::milliseconds, std::uint32_t>
                  == sizeof(std::int64_t));
    static_assert(serializedSizeV<std::chrono::seconds, std::uint32_t> == sizeof(std::int64_t));
    static_assert(serializedSizeV<std::chrono::minutes, std::uint32_t> == sizeof(std::int32_t));
    static_assert(serializedSizeV<std::chrono::hours, std::uint32_t> == sizeof(std::int32_t));

    static_assert(serializedSizeV<std::pair<std::uint8_t, std::uint32_t>, std::uint32_t> == 5);
    static_assert(serializedSizeV<std::tuple<std::uint8_t, std::uint16_t>, std::uint32_t> == 3);
    static_assert(
      serializedSizeV<std::tuple<std::uint8_t, std::uint16_t, std::uint32_t>, std::uint32_t> == 7);
    static_assert(serializedSizeV<std::tuple<double, double>, std::uint32_t> == 16);
    static_assert(serializedSizeV<std::tuple<std::uint32_t>, std::uint32_t> == 4);
    static_assert(serializedSizeV<std::pair<std::pair<std::uint8_t, std::uint8_t>, std::uint16_t>,
                                  std::uint32_t>
                  == 4);

    static_assert(serializedSizeV<Test::TypeSerializer::TwoFieldAggregate, std::uint32_t> == 2);
    static_assert(serializedSizeV<Types::Empty, std::uint32_t> == 0);
    static_assert(serializedSizeV<Types::Enum, std::uint32_t>
                  == sizeof(std::uint8_t) + sizeof(int));
    static_assert(serializedSizeV<Types::Chrono, std::uint32_t>
                  == 3 * sizeof(std::int64_t) + 2 * sizeof(std::int32_t));
    static_assert(serializedSizeV<Types::Primitive, std::uint32_t> == 43);

    static_assert(serializedSizeV<std::array<int, 5>, std::uint32_t> == 4 + 5 * 4);
    static_assert(serializedSizeV<std::array<std::uint8_t, 3>, std::uint32_t> == 4 + 3);
    static_assert(serializedSizeV<std::array<int, 0>, std::uint32_t> == 4);
    static_assert(serializedSizeV<std::array<std::array<int, 2>, 3>, std::uint32_t>
                  == 4 + 3 * (4 + 2 * 4));
    static_assert(aglio::HasFixedSerializedSize<std::array<int, 5>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::array<std::string, 2>, std::uint32_t>);

    static_assert(aglio::HasFixedSerializedSize<std::uint32_t, std::uint32_t>);
    static_assert(
      aglio::HasFixedSerializedSize<Test::TypeSerializer::TwoFieldAggregate, std::uint32_t>);
    static_assert(aglio::HasFixedSerializedSize<Types::Empty, std::uint32_t>);
    static_assert(aglio::HasFixedSerializedSize<Types::Primitive, std::uint32_t>);
    static_assert(aglio::HasFixedSerializedSize<Types::Chrono, std::uint32_t>);
    static_assert(aglio::HasFixedSerializedSize<Types::Enum, std::uint32_t>);
    static_assert(aglio::HasFixedSerializedSize<std::chrono::milliseconds, std::uint32_t>);
    static_assert(aglio::HasFixedSerializedSize<std::pair<int, float>, std::uint32_t>);

    static_assert(!aglio::HasFixedSerializedSize<std::vector<int>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::string, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::map<int, int>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::set<int>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::optional<int>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::variant<int, float>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::expected<int, std::string>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<Types::Container, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<Types::Nested, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::pair<int, std::string>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::tuple<int, std::vector<int>>, std::uint32_t>);

    static_assert(serializedSizeV<std::uint32_t, std::uint8_t> == 4);
    static_assert(serializedSizeV<std::uint32_t, std::uint16_t> == 4);
    static_assert(serializedSizeV<Types::Primitive, std::uint8_t> == 43);
    static_assert(serializedSizeV<Types::Primitive, std::uint16_t> == 43);

    CHECK(true);
}

TEST_CASE("booleans are serialized to 0 or 1",
          "[serializer]") {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView view{buffer};

    REQUIRE(aglio::TypeSerializer<bool, std::uint32_t>::serialize(true, view));
    REQUIRE(!buffer.empty());
    CHECK(buffer[0] == std::byte{1});

    REQUIRE(aglio::TypeSerializer<bool, std::uint32_t>::serialize(false, view));
    REQUIRE(!buffer.empty());
    CHECK(buffer[1] == std::byte{0});
}

TEST_CASE("Serializer: a contiguous range of bool holds true or false whatever the wire says",
          "[serializer]") {
    using S = std::uint8_t;
    // What a bool object holds is looked at as a byte: reading a bool that holds 2 is undefined.
    using Raw = std::array<std::uint8_t, 4>;
    static constexpr Raw want{0, 1, 1, 1};

    // the count, then one byte per element: 0, 1 and two that are neither
    static constexpr std::array<std::byte, 5> wire{std::byte{4},
                                                   std::byte{0},
                                                   std::byte{1},
                                                   std::byte{2},
                                                   std::byte{0xFF}};

    SECTION("std::array<bool, N>") {
        std::array<bool, 4>               out{};
        aglio::DynamicDeserializationView de{wire};
        REQUIRE(aglio::TypeSerializer<std::array<bool, 4>, S>::deserialize(out, de));
        CHECK(std::bit_cast<Raw>(out) == want);
        CHECK(de.available() == 0);
    }

    SECTION("a static vector of bool") {
        Types::StaticVec<bool, 4>         out{};
        aglio::DynamicDeserializationView de{wire};
        REQUIRE(aglio::TypeSerializer<Types::StaticVec<bool, 4>, S>::deserialize(out, de));
        REQUIRE(out.size() == 4);
        CHECK(std::bit_cast<Raw>(out.storage) == want);
    }

    SECTION("in a struct of fixed serialized size, read through one claim") {
        using T = std::pair<std::uint8_t, std::array<bool, 4>>;
        static constexpr std::array<std::byte, 6> framed{std::byte{9},
                                                         std::byte{4},
                                                         std::byte{0},
                                                         std::byte{1},
                                                         std::byte{2},
                                                         std::byte{0xFF}};
        T                                         out{};
        aglio::DynamicDeserializationView         de{framed};
        REQUIRE(aglio::TypeSerializer<T, S>::deserialize(out, de));
        CHECK(out.first == 9);
        CHECK(std::bit_cast<Raw>(out.second) == want);
    }

    SECTION("what is written is 0 or 1, and comes back") {
        using namespace Test::TypeSerializer;
        CHECK(roundTripCheck<S>(std::array<bool, 4>{true, false, true, true}));
        CHECK(roundTripCheck<S>(Types::StaticVec<bool, 4>{true, false, true}));
        CHECK(roundTripCheck<S>(Types::createDefault<Types::BoolRanges>()));

        std::vector<std::byte>          buffer;
        aglio::DynamicSerializationView ser{buffer};
        REQUIRE(
          aglio::TypeSerializer<std::array<bool, 2>, S>::serialize(std::array<bool, 2>{true, false},
                                                                   ser));
        REQUIRE(buffer.size() == 3);
        CHECK(buffer[0] == std::byte{2});
        CHECK(buffer[1] == std::byte{1});
        CHECK(buffer[2] == std::byte{0});
    }
}

TEST_CASE("Serializer: every kind of leaf round-trips, in a range too",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S = std::uint16_t;

    CHECK(roundTripCheck<S>(std::int8_t{-5}));
    CHECK(roundTripCheck<S>(std::uint16_t{0xBEEF}));
    CHECK(roundTripCheck<S>(std::int32_t{-123456}));
    CHECK(roundTripCheck<S>(std::uint64_t{0x0123'4567'89AB'CDEFULL}));
    CHECK(roundTripCheck<S>(char{'x'}));
    CHECK(roundTripCheck<S>(char8_t{0xC3}));
    CHECK(roundTripCheck<S>(char16_t{0x20AC}));
    CHECK(roundTripCheck<S>(std::byte{0xA5}));
    CHECK(roundTripCheckFrom<S>(Types::Color::green, Types::Color::red));
    CHECK(roundTripCheck<S>(Types::inactive));
    // a value without an enumerator: the fixed underlying type is what makes it one
    CHECK(roundTripCheckFrom<S>(static_cast<Types::Color>(200), Types::Color::red));
    CHECK(roundTripCheck<S>(true));

    // bit patterns, not values: a NaN and a negative zero come back as the bytes they were
    auto const sameBits = [](auto in) {
        decltype(in) out{};
        return roundTrip<S>(in, out)
            && std::bit_cast<std::array<std::byte, sizeof(in)>>(in)
                 == std::bit_cast<std::array<std::byte, sizeof(in)>>(out);
    };
    CHECK(sameBits(1.5f));
    CHECK(sameBits(-0.0f));
    CHECK(sameBits(std::numeric_limits<float>::quiet_NaN()));
    CHECK(sameBits(std::numeric_limits<double>::denorm_min()));
    CHECK(sameBits(-2.5e300));

    CHECK(roundTripCheck<S>(std::array<std::uint16_t, 3>{1, 0x8000, 0xFFFF}));
    CHECK(roundTripCheck<S>(std::vector<std::int64_t>{-1, 0, 1}));
    CHECK(roundTripCheck<S>(std::vector<std::byte>{std::byte{0}, std::byte{0xFF}}));
    CHECK(
      roundTripCheckFrom<S>(std::array<Types::Color, 2>{Types::Color::red, Types::Color::blue},
                            std::array<Types::Color, 2>{Types::Color::green, Types::Color::green}));
    CHECK(roundTripCheck<S>(std::vector<Types::Status>{Types::active, Types::unknown}));
    CHECK(roundTripCheck<S>(std::string{"wire"}));
    CHECK(roundTripCheck<S>(Types::StaticVec<std::uint32_t, 4>{7U, 8U, 9U}));
}

namespace Test::TypeSerializer {
using RangeElements = std::tuple<std::uint8_t,
                                 std::int16_t,
                                 std::uint32_t,
                                 std::uint64_t,
                                 float,
                                 double,
                                 char,
                                 std::byte,
                                 Types::Color>;
}   // namespace Test::TypeSerializer

TEMPLATE_LIST_TEST_CASE("Serializer: a range of numbers is read out of the bytes wherever they lie",
                        "[serializer]",
                        Test::TypeSerializer::RangeElements) {
    using S    = std::uint16_t;
    using Elem = TestType;

    // an element from a pattern of bytes, made a value the way the serializer makes one
    auto const element = [](std::size_t seed) {
        std::array<std::byte, sizeof(Elem)> pattern{};
        for(std::size_t b = 0; b != sizeof(Elem); ++b) {
            pattern[b] = static_cast<std::byte>((seed * 7U + b * 13U) & 0xFFU);
        }
        return std::bit_cast<Elem>(pattern);
    };

    // every length up to a few hundred elements, behind 0..7 bytes of something else: the elements'
    // bytes start at every alignment
    for(std::size_t const count : {0U, 1U, 2U, 3U, 7U, 8U, 31U, 64U, 257U}) {
        for(std::size_t lead = 0; lead != 8; ++lead) {
            std::vector<Elem> in;
            for(std::size_t i = 0; i != count; ++i) { in.push_back(element(i + lead)); }

            std::vector<std::byte>          buffer(lead, std::byte{0xEE});
            std::vector<std::byte>          body;
            aglio::DynamicSerializationView bodySer{body};
            REQUIRE(aglio::TypeSerializer<std::vector<Elem>, S>::serialize(in, bodySer));
            buffer.insert(buffer.end(), body.begin(), body.end());
            buffer.push_back(std::byte{0x77});   // and something behind

            std::span<std::byte const> const  wire{buffer};
            auto const                        from = wire.subspan(lead);
            aglio::DynamicDeserializationView de{from};
            std::vector<Elem>                 out;
            REQUIRE(aglio::TypeSerializer<std::vector<Elem>, S>::deserialize(out, de));
            REQUIRE(out.size() == count);
            bool const sameBytes
              = std::ranges::equal(std::as_bytes(std::span{in}), std::as_bytes(std::span{out}));
            CHECK(sameBytes);
            CHECK(de.available() == 1);

            // one byte short: refused
            if(count != 0) {
                auto const cut = from.first(sizeof(S) + count * sizeof(Elem) - 1);
                aglio::DynamicDeserializationView cutDe{cut};
                std::vector<Elem>                 none;
                CHECK(!aglio::TypeSerializer<std::vector<Elem>, S>::deserialize(none, cutDe));
            }

            // through a stream: no bytes to look at, a block at a time (several blocks at 257 elements)
            std::string text;
            for(auto const b : body) { text.push_back(static_cast<char>(b)); }
            std::istringstream               stream{text};
            aglio::StreamDeserializationView streamDe{stream};
            std::vector<Elem>                viaStream;
            REQUIRE(aglio::TypeSerializer<std::vector<Elem>, S>::deserialize(viaStream, streamDe));
            bool const sameViaStream = std::ranges::equal(std::as_bytes(std::span{in}),
                                                          std::as_bytes(std::span{viaStream}));
            CHECK(sameViaStream);
        }
    }

    // inside a struct of fixed serialized size the bytes come out of the one claim
    using Fixed = std::pair<std::uint8_t, std::array<Elem, 3>>;
    Fixed const in{
      std::uint8_t{9},
      {element(40), element(41), element(42)}
    };
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::TypeSerializer<Fixed, S>::serialize(in, ser));
    aglio::DynamicDeserializationView de{buffer};
    Fixed                             out{
      std::uint8_t{0},
      {element(1), element(2), element(3)}
    };
    REQUIRE(aglio::TypeSerializer<Fixed, S>::deserialize(out, de));
    bool const same = out.first == in.first
                   && std::ranges::equal(std::as_bytes(std::span{in.second}),
                                         std::as_bytes(std::span{out.second}));
    CHECK(same);
}

namespace Test::TypeSerializer {
using OtherBytes = std::tuple<std::uint8_t, char, signed char>;
}   // namespace Test::TypeSerializer

TEMPLATE_LIST_TEST_CASE("Serializer: a buffer of another kind of byte holds the same bytes",
                        "[serializer]",
                        Test::TypeSerializer::OtherBytes) {
    using S    = std::uint16_t;
    using Byte = TestType;

    // a struct of fixed size (one claim of a std::byte buffer, field by field into any other), ranges
    // (copied at once), wrappers
    auto const check = [](auto const& value) {
        using T = std::remove_cvref_t<decltype(value)>;

        std::vector<std::byte>          reference;
        aglio::DynamicSerializationView referenceSer{reference};
        REQUIRE(aglio::TypeSerializer<T, S>::serialize(value, referenceSer));

        std::vector<Byte>               other;
        aglio::DynamicSerializationView otherSer{other};
        REQUIRE(aglio::TypeSerializer<T, S>::serialize(value, otherSer));

        REQUIRE(other.size() == reference.size());
        bool const sameBytes = std::ranges::equal(reference, other, [](std::byte a, Byte b) {
            return a == static_cast<std::byte>(b);
        });
        CHECK(sameBytes);

        // and back out of it, through a view that cannot be written
        std::span<Byte const> const       wire{other};
        aglio::DynamicDeserializationView de{wire};
        T                                 out{};
        REQUIRE(aglio::TypeSerializer<T, S>::deserialize(out, de));
        CHECK(de.available() == 0);
        bool const same = out == value;
        CHECK(same);

        // cut short: refused
        if(!other.empty()) {
            auto const                        cut = wire.first(wire.size() - 1);
            aglio::DynamicDeserializationView cutDe{cut};
            T                                 none{};
            CHECK(!aglio::TypeSerializer<T, S>::deserialize(none, cutDe));
        }
    };

    check(Types::createDefault<Types::Primitive>());
    check(Types::createDefault<Types::Container>());
    check(Types::createDefault<Types::WrapperNoFloat>());
    check(Types::createDefault<Types::Chrono>());
    check(Types::createDefault<Types::Nested>());
    check(Types::createDefault<Types::BoolRanges>());
    check(std::vector<std::uint64_t>{1, 0x8000'0000'0000'0000ULL, 0xFFFF'FFFF'FFFF'FFFFULL});
    check(std::string{"bytes of another kind"});
    check(std::array<std::uint16_t, 3>{1, 2, 0xFFFF});
}

TEST_CASE("serializedSizeV matches actual serialized byte count",
          "[serializer][SerializedSize]") {
    auto const check = [](auto const& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        using S = std::uint32_t;

        std::vector<std::byte>          buffer;
        aglio::DynamicSerializationView ser{buffer};
        REQUIRE(aglio::TypeSerializer<T, S>::serialize(value, ser));
        CHECK(buffer.size() == aglio::serializedSizeV<T, S>);
    };

    SECTION("trivial") {
        check(std::uint8_t{42});
        check(std::uint32_t{123456});
        check(double{3.14});
        check(true);
    }

    SECTION("enum") {
        check(Types::Color::blue);
        check(Types::active);
    }

    SECTION("chrono") {
        check(std::chrono::milliseconds{500});
        check(std::chrono::nanoseconds{123456789});
    }

    SECTION("tuple-like") {
        check(std::pair<std::uint8_t, std::uint32_t>{1, 2});
        check(std::tuple<std::uint8_t, std::uint16_t, std::uint32_t>{1, 2, 3});
    }

    SECTION("fixed-size range") {
        check(std::array<int, 5>{1, 2, 3, 4, 5});
        check(std::array<std::uint8_t, 3>{10, 20, 30});
        check(std::array<int, 0>{});
    }

    SECTION("described struct") {
        check(Types::createDefault<Types::Primitive>());
        check(Types::createDefault<Types::Chrono>());
        check(Types::createDefault<Types::Enum>());
        check(Types::createDefault<Types::Empty>());
        check(Test::TypeSerializer::TwoFieldAggregate{.a = 7, .b = 3});
    }
}

TEST_CASE("Serializer: stream views round-trip",
          "[serializer][stream]") {
    using S = std::uint32_t;

    Types::Primitive pIn = Types::createDefault<Types::Primitive>();

    std::ostringstream             oss;
    aglio::StreamSerializationView sser{oss};
    REQUIRE(aglio::TypeSerializer<Types::Primitive, S>::serialize(pIn, sser));

    std::string const                data = oss.str();
    std::istringstream               iss(data);
    aglio::StreamDeserializationView sde{iss};

    Types::Primitive pOut{};
    REQUIRE(aglio::TypeSerializer<Types::Primitive, S>::deserialize(pOut, sde));
    CHECK(pIn == pOut);
}

TEST_CASE("Serializer: stream deserialization truncated",
          "[serializer][stream]") {
    using S = std::uint32_t;

    std::istringstream               iss(std::string(2, '\0'));
    aglio::StreamDeserializationView sde{iss};

    Types::Primitive pOut{};
    CHECK(!aglio::TypeSerializer<Types::Primitive, S>::deserialize(pOut, sde));
}

namespace Test::TypeSerializer {

/// A vector with its bound in its type, like a static vector: capacity() is static.
template<typename T, std::size_t N>
struct Capped : std::vector<T> {
    using std::vector<T>::vector;

    static constexpr std::size_t capacity() { return N; }
};

/// A map with its bound in its type, like a static map.
template<typename K, typename V, std::size_t N>
struct CappedMap : std::map<K, V> {
    static constexpr std::size_t capacity() { return N; }
};

struct WithCapped {
    std::uint16_t               id{};
    Capped<std::uint32_t, 3>    values;
    std::optional<std::uint8_t> flag;
    std::variant<bool, double>  either;
};

struct WithVector {
    std::uint8_t              id{};
    std::vector<std::uint8_t> bytes;
};

/// Not an aggregate, so only its own serializer below describes it: a wire format the generic
/// rules cannot know (one byte for a value that is 32 bits in memory).
class Squeezed {
    std::uint32_t v_{};

public:
    constexpr Squeezed() = default;

    [[nodiscard]] constexpr std::uint32_t value() const { return v_; }
};

}   // namespace Test::TypeSerializer

template<typename SizeT>
struct aglio::TypeSerializer<Test::TypeSerializer::Squeezed, SizeT> {
    static constexpr std::size_t maxSerializedSize = 1;
};

TEST_CASE("maxSerializedSizeV compile-time values",
          "[serializer][MaxSerializedSize]") {
    using aglio::HasMaxSerializedSize;
    using aglio::maxSerializedSizeV;
    using Test::TypeSerializer::Capped;
    using Test::TypeSerializer::CappedMap;
    using S = std::uint16_t;

    // A fixed size is its own worst case.
    static_assert(maxSerializedSizeV<std::uint32_t, S> == 4);
    static_assert(maxSerializedSizeV<bool, S> == 1);
    static_assert(maxSerializedSizeV<Types::Primitive, S>
                  == aglio::serializedSizeV<Types::Primitive, S>);
    static_assert(maxSerializedSizeV<std::array<int, 5>, S> == 2 + 5 * 4);

    // Engaged, failed or the largest alternative: flag or index, then the value.
    static_assert(maxSerializedSizeV<std::optional<std::uint32_t>, S> == 1 + 4);
    static_assert(maxSerializedSizeV<std::expected<std::uint32_t, std::uint8_t>, S> == 1 + 4);
    static_assert(maxSerializedSizeV<std::expected<std::uint8_t, std::uint64_t>, S> == 1 + 8);
    static_assert(maxSerializedSizeV<std::expected<void, std::uint16_t>, S> == 1 + 2);
    static_assert(maxSerializedSizeV<std::variant<std::uint8_t, double, bool>, S> == 1 + 8);
    static_assert(maxSerializedSizeV<std::chrono::milliseconds, S> == 8);
    static_assert(maxSerializedSizeV<std::tuple<std::uint8_t, std::optional<std::uint16_t>>, S>
                  == 1 + 1 + 2);

    // A static capacity bounds a range: size field, then that many elements at their largest.
    static_assert(maxSerializedSizeV<Capped<std::uint32_t, 3>, S> == 2 + 3 * 4);
    static_assert(maxSerializedSizeV<Capped<std::optional<std::uint8_t>, 2>, S> == 2 + 2 * 2);
    static_assert(maxSerializedSizeV<CappedMap<std::uint8_t, std::uint32_t, 4>, S> == 2 + 4 * 5);
    static_assert(maxSerializedSizeV<Test::TypeSerializer::WithCapped, S>
                  == 2 + (2 + 3 * 4) + (1 + 1) + (1 + 8));
    // ...but never past what the size field can count.
    static_assert(maxSerializedSizeV<Capped<std::uint8_t, 1000>, std::uint8_t> == 1 + 255);
    static_assert(maxSerializedSizeV<Capped<std::vector<int>, 0>, S> == 2);

    // A serializer's own bound wins over the shape.
    static_assert(maxSerializedSizeV<Test::TypeSerializer::Squeezed, S> == 1);
    static_assert(maxSerializedSizeV<Capped<Test::TypeSerializer::Squeezed, 3>, S> == 2 + 3);

    // Nothing bounds a std::vector, std::string or std::map, nor what holds one.
    static_assert(!HasMaxSerializedSize<std::vector<int>, S>);
    static_assert(!HasMaxSerializedSize<std::string, S>);
    static_assert(!HasMaxSerializedSize<std::map<int, int>, S>);
    static_assert(!HasMaxSerializedSize<std::optional<std::vector<int>>, S>);
    static_assert(!HasMaxSerializedSize<std::variant<std::uint8_t, std::string>, S>);
    static_assert(!HasMaxSerializedSize<Test::TypeSerializer::WithVector, S>);
    static_assert(!HasMaxSerializedSize<Capped<std::vector<int>, 2>, S>);
    static_assert(HasMaxSerializedSize<std::uint8_t, S>);

    // A bound past size_t saturates to "none" instead of wrapping into a small number.
    constexpr std::size_t huge = std::numeric_limits<std::size_t>::max() / 4;
    static_assert(!HasMaxSerializedSize<Capped<std::uint64_t, huge>, std::uint64_t>);
    static_assert(!HasMaxSerializedSize<
                  std::tuple<Capped<std::uint8_t, huge * 3>, Capped<std::uint8_t, huge * 3>>,
                  std::uint64_t>);
}

TEST_CASE("A value at its largest serializes to maxSerializedSizeV",
          "[serializer][MaxSerializedSize]") {
    using S = std::uint16_t;
    Test::TypeSerializer::WithCapped const v{
      .id     = 7,
      .values = {1, 2, 3},
      .flag   = std::uint8_t{4},
      .either = 2.5
    };

    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::TypeSerializer<Test::TypeSerializer::WithCapped, S>::serialize(v, ser));
    CHECK(buffer.size() == aglio::maxSerializedSizeV<Test::TypeSerializer::WithCapped, S>);
}

#ifdef AGLIO_USE_MP_UNITS
TEST_CASE("Serializer: mp-units quantities as their number, offset units too",
          "[serializer]") {
    using namespace Test::TypeSerializer;
    using S        = std::uint32_t;
    namespace si   = mp_units::si;
    using DeciDegC = mp_units::quantity<si::deci<si::degree_Celsius>, std::int16_t>;
    using MilliAmp = mp_units::quantity<si::milli<si::ampere>, std::uint32_t>;
    using Metre    = mp_units::quantity<si::metre, double>;

    CHECK(roundTripCheck<S>(DeciDegC{std::int16_t{-12}, si::deci<si::degree_Celsius>}));
    CHECK(roundTripCheck<S>(
      DeciDegC{std::numeric_limits<std::int16_t>::min(), si::deci<si::degree_Celsius>}));
    CHECK(roundTripCheck<S>(MilliAmp{4'200U, si::milli<si::ampere>}));
    CHECK(roundTripCheck<S>(Metre{1.25, si::metre}));
    // An optional quantity is compared by hand: libc++ lacks LWG 4072, so its
    // optional<Q> == optional<Q> also tries optional<Q> == U with U = optional<Q>, whose
    // Q == optional<Q> asks mp-units' RepresentationOf for equality_comparable<optional<Q>>
    // again ("satisfaction of constraint ... depends on itself").
    DeciDegC const          hot{std::int16_t{4'500}, si::deci<si::degree_Celsius>};
    std::optional<DeciDegC> none{hot};   // filled, so the empty one has to clear it
    CHECK(roundTrip<S>(std::optional<DeciDegC>{}, none));
    CHECK(!none.has_value());
    std::optional<DeciDegC> some;
    CHECK(roundTrip<S>(std::optional<DeciDegC>{hot}, some));
    CHECK((some.has_value() && *some == hot));

    // the wire is the rep alone: as many bytes as a bare int16_t, and the same ones
    std::vector<std::byte>          q;
    std::vector<std::byte>          raw;
    aglio::DynamicSerializationView qs{q};
    aglio::DynamicSerializationView rs{raw};
    CHECK(aglio::TypeSerializer<DeciDegC, S>::serialize(
      DeciDegC{std::int16_t{-12}, si::deci<si::degree_Celsius>},
      qs));
    CHECK(aglio::TypeSerializer<std::int16_t, S>::serialize(std::int16_t{-12}, rs));
    bool const sameBytes
      = q == raw;   // a bool: Catch2 would print the vectors, and std::byte has no printer
    CHECK(sameBytes);
    STATIC_CHECK(aglio::serializedSizeV<DeciDegC, S> == sizeof(std::int16_t));
    STATIC_CHECK(aglio::MaxSerializedSize<std::optional<DeciDegC>, S>::value
                 == 1 + sizeof(std::int16_t));
}
#endif
