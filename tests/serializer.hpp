#pragma once

#include "types.hpp"

#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace Test::serializer {

template<typename SizeT,
         typename T>
bool roundTrip(T const& in,
               T&       out) {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    if(!aglio::serializer<T, SizeT>::serialize(in, ser)) { return false; }
    aglio::DynamicDeserializationView de{buffer};
    return aglio::serializer<T, SizeT>::deserialize(out, de);
}

template<typename SizeT,
         typename T>
bool roundTripCheck(T const& in) {
    T out{};
    if(!roundTrip<SizeT>(in, out)) { return false; }
    return in == out;
}

struct TwoFieldAggregate {
    std::uint8_t a{};
    std::uint8_t b{};
    bool         operator==(TwoFieldAggregate const&) const = default;
};

}   // namespace Test::serializer

TEST_CASE("Serializer: empty containers round-trip",
          "[serializer]") {
    using namespace Test::serializer;
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
    bool const ok = aglio::serializer<std::vector<std::uint8_t>, std::uint8_t>::serialize(big, ser);
    CHECK(!ok);
}

TEST_CASE("Serializer: variant out-of-bounds index",
          "[serializer]") {
    using V = std::variant<int, float>;
    using S = std::uint32_t;

    V const                         vIn = 42;
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::serializer<V, S>::serialize(vIn, ser));

    buffer[0] = std::byte{5};

    V                                 vOut;
    aglio::DynamicDeserializationView de{buffer};
    CHECK(!aglio::serializer<V, S>::deserialize(vOut, de));
}

TEST_CASE("Serializer: deserialize from truncated buffer",
          "[serializer]") {
    using S = std::uint32_t;

    Types::Primitive const          pIn = Types::createDefault<Types::Primitive>();
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::serializer<Types::Primitive, S>::serialize(pIn, ser));

    buffer.resize(buffer.size() / 2);

    Types::Primitive                  pOut{};
    aglio::DynamicDeserializationView de{buffer};
    CHECK(!aglio::serializer<Types::Primitive, S>::deserialize(pOut, de));
}

TEST_CASE("Serializer: optional round-trip both states",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;

    CHECK(roundTripCheck<S>(std::optional<int>{42}));
    CHECK(roundTripCheck<S>(std::optional<int>{std::nullopt}));
}

TEST_CASE("Serializer: expected round-trip both states",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;
    using E = std::expected<int, std::string>;

    CHECK(roundTripCheck<S>(E{42}));
    CHECK(roundTripCheck<S>(E{std::unexpected{std::string("err")}}));
}

TEST_CASE("Serializer: variant round-trip each alternative",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;
    using V = std::variant<int, std::string, float>;

    CHECK(roundTripCheck<S>(V{123}));
    CHECK(roundTripCheck<S>(V{std::string("hello")}));
    CHECK(roundTripCheck<S>(V{3.14f}));
}

TEST_CASE("Serializer: chrono duration round-trip",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;

    CHECK(roundTripCheck<S>(std::chrono::milliseconds{12345}));
    CHECK(roundTripCheck<S>(std::chrono::nanoseconds{9876543210LL}));
}

TEST_CASE("Serializer: nested containers with mixed optional",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;

    std::vector<std::optional<int>> const v{1, std::nullopt, 3, std::nullopt, 5};
    CHECK(roundTripCheck<S>(v));
}

TEST_CASE("serialized_size_v compile-time values",
          "[serializer][serialized_size]") {
    using aglio::serialized_size_v;

    static_assert(serialized_size_v<std::uint8_t, std::uint32_t> == 1);
    static_assert(serialized_size_v<std::uint16_t, std::uint32_t> == 2);
    static_assert(serialized_size_v<std::uint32_t, std::uint32_t> == 4);
    static_assert(serialized_size_v<std::int64_t, std::uint32_t> == 8);
    static_assert(serialized_size_v<float, std::uint32_t> == 4);
    static_assert(serialized_size_v<double, std::uint32_t> == 8);
    static_assert(serialized_size_v<bool, std::uint32_t> == 1);

    static_assert(serialized_size_v<Types::Color, std::uint32_t> == sizeof(std::uint8_t));
    static_assert(serialized_size_v<Types::Status, std::uint32_t> == sizeof(int));

    static_assert(serialized_size_v<std::chrono::nanoseconds, std::uint32_t>
                  == sizeof(std::int64_t));
    static_assert(serialized_size_v<std::chrono::milliseconds, std::uint32_t>
                  == sizeof(std::int64_t));
    static_assert(serialized_size_v<std::chrono::seconds, std::uint32_t> == sizeof(std::int64_t));
    static_assert(serialized_size_v<std::chrono::minutes, std::uint32_t> == sizeof(std::int64_t));
    static_assert(serialized_size_v<std::chrono::hours, std::uint32_t> == sizeof(std::int64_t));

    static_assert(serialized_size_v<std::pair<std::uint8_t, std::uint32_t>, std::uint32_t> == 5);
    static_assert(serialized_size_v<std::tuple<std::uint8_t, std::uint16_t>, std::uint32_t> == 3);
    static_assert(
      serialized_size_v<std::tuple<std::uint8_t, std::uint16_t, std::uint32_t>, std::uint32_t>
      == 7);
    static_assert(serialized_size_v<std::tuple<double, double>, std::uint32_t> == 16);
    static_assert(serialized_size_v<std::tuple<std::uint32_t>, std::uint32_t> == 4);
    static_assert(serialized_size_v<std::pair<std::pair<std::uint8_t, std::uint8_t>, std::uint16_t>,
                                    std::uint32_t>
                  == 4);

    static_assert(serialized_size_v<Test::serializer::TwoFieldAggregate, std::uint32_t> == 2);
    static_assert(serialized_size_v<Types::Empty, std::uint32_t> == 0);
    static_assert(serialized_size_v<Types::Enum, std::uint32_t>
                  == sizeof(std::uint8_t) + sizeof(int));
    static_assert(serialized_size_v<Types::Chrono, std::uint32_t> == 5 * sizeof(std::int64_t));
    static_assert(serialized_size_v<Types::Primitive, std::uint32_t> == 43);

    static_assert(serialized_size_v<std::array<int, 5>, std::uint32_t> == 4 + 5 * 4);
    static_assert(serialized_size_v<std::array<std::uint8_t, 3>, std::uint32_t> == 4 + 3);
    static_assert(serialized_size_v<std::array<int, 0>, std::uint32_t> == 4);
    static_assert(serialized_size_v<std::array<std::array<int, 2>, 3>, std::uint32_t>
                  == 4 + 3 * (4 + 2 * 4));
    static_assert(aglio::HasFixedSerializedSize<std::array<int, 5>, std::uint32_t>);
    static_assert(!aglio::HasFixedSerializedSize<std::array<std::string, 2>, std::uint32_t>);

    static_assert(aglio::HasFixedSerializedSize<std::uint32_t, std::uint32_t>);
    static_assert(
      aglio::HasFixedSerializedSize<Test::serializer::TwoFieldAggregate, std::uint32_t>);
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

    static_assert(serialized_size_v<std::uint32_t, std::uint8_t> == 4);
    static_assert(serialized_size_v<std::uint32_t, std::uint16_t> == 4);
    static_assert(serialized_size_v<Types::Primitive, std::uint8_t> == 43);
    static_assert(serialized_size_v<Types::Primitive, std::uint16_t> == 43);

    CHECK(true);
}

TEST_CASE("booleans are serialized to 0 or 1",
          "[serializer]") {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView view{buffer};

    REQUIRE(aglio::serializer<bool, std::uint32_t>::serialize(true, view));
    REQUIRE(!buffer.empty());
    CHECK(buffer[0] == std::byte{1});

    REQUIRE(aglio::serializer<bool, std::uint32_t>::serialize(false, view));
    REQUIRE(!buffer.empty());
    CHECK(buffer[1] == std::byte{0});
}

TEST_CASE("serialized_size_v matches actual serialized byte count",
          "[serializer][serialized_size]") {
    auto const check = [](auto const& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        using S = std::uint32_t;

        std::vector<std::byte>          buffer;
        aglio::DynamicSerializationView ser{buffer};
        REQUIRE(aglio::serializer<T, S>::serialize(value, ser));
        CHECK(buffer.size() == aglio::serialized_size_v<T, S>);
    };

    SECTION("trivial") {
        check(std::uint8_t{42});
        check(std::uint32_t{123456});
        check(double{3.14});
        check(true);
    }

    SECTION("enum") {
        check(Types::Color::Blue);
        check(Types::Active);
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
        check(Test::serializer::TwoFieldAggregate{.a = 7, .b = 3});
    }
}

TEST_CASE("Serializer: stream views round-trip",
          "[serializer][stream]") {
    using S = std::uint32_t;

    Types::Primitive pIn = Types::createDefault<Types::Primitive>();

    std::ostringstream             oss;
    aglio::StreamSerializationView sser{oss};
    REQUIRE(aglio::serializer<Types::Primitive, S>::serialize(pIn, sser));

    std::string const                data = oss.str();
    std::istringstream               iss(data);
    aglio::StreamDeserializationView sde{iss};

    Types::Primitive pOut{};
    REQUIRE(aglio::serializer<Types::Primitive, S>::deserialize(pOut, sde));
    CHECK(pIn == pOut);
}

TEST_CASE("Serializer: stream deserialization truncated",
          "[serializer][stream]") {
    using S = std::uint32_t;

    std::istringstream               iss(std::string(2, '\0'));
    aglio::StreamDeserializationView sde{iss};

    Types::Primitive pOut{};
    CHECK(!aglio::serializer<Types::Primitive, S>::deserialize(pOut, sde));
}

namespace Test::serializer {

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

}   // namespace Test::serializer

template<typename SizeT>
struct aglio::serializer<Test::serializer::Squeezed, SizeT> {
    static constexpr std::size_t max_serialized_size = 1;
};

TEST_CASE("max_serialized_size_v compile-time values",
          "[serializer][max_serialized_size]") {
    using aglio::HasMaxSerializedSize;
    using aglio::max_serialized_size_v;
    using Test::serializer::Capped;
    using Test::serializer::CappedMap;
    using S = std::uint16_t;

    // A fixed size is its own worst case.
    static_assert(max_serialized_size_v<std::uint32_t, S> == 4);
    static_assert(max_serialized_size_v<bool, S> == 1);
    static_assert(max_serialized_size_v<Types::Primitive, S>
                  == aglio::serialized_size_v<Types::Primitive, S>);
    static_assert(max_serialized_size_v<std::array<int, 5>, S> == 2 + 5 * 4);

    // Engaged, failed or the largest alternative: flag or index, then the value.
    static_assert(max_serialized_size_v<std::optional<std::uint32_t>, S> == 1 + 4);
    static_assert(max_serialized_size_v<std::expected<std::uint32_t, std::uint8_t>, S> == 1 + 4);
    static_assert(max_serialized_size_v<std::expected<std::uint8_t, std::uint64_t>, S> == 1 + 8);
    static_assert(max_serialized_size_v<std::expected<void, std::uint16_t>, S> == 1 + 2);
    static_assert(max_serialized_size_v<std::variant<std::uint8_t, double, bool>, S> == 1 + 8);
    static_assert(max_serialized_size_v<std::chrono::milliseconds, S> == 8);
    static_assert(max_serialized_size_v<std::tuple<std::uint8_t, std::optional<std::uint16_t>>, S>
                  == 1 + 1 + 2);

    // A static capacity bounds a range: size field, then that many elements at their largest.
    static_assert(max_serialized_size_v<Capped<std::uint32_t, 3>, S> == 2 + 3 * 4);
    static_assert(max_serialized_size_v<Capped<std::optional<std::uint8_t>, 2>, S> == 2 + 2 * 2);
    static_assert(max_serialized_size_v<CappedMap<std::uint8_t, std::uint32_t, 4>, S> == 2 + 4 * 5);
    static_assert(max_serialized_size_v<Test::serializer::WithCapped, S>
                  == 2 + (2 + 3 * 4) + (1 + 1) + (1 + 8));
    // ...but never past what the size field can count.
    static_assert(max_serialized_size_v<Capped<std::uint8_t, 1000>, std::uint8_t> == 1 + 255);
    static_assert(max_serialized_size_v<Capped<std::vector<int>, 0>, S> == 2);

    // A serializer's own bound wins over the shape.
    static_assert(max_serialized_size_v<Test::serializer::Squeezed, S> == 1);
    static_assert(max_serialized_size_v<Capped<Test::serializer::Squeezed, 3>, S> == 2 + 3);

    // Nothing bounds a std::vector, std::string or std::map, nor what holds one.
    static_assert(!HasMaxSerializedSize<std::vector<int>, S>);
    static_assert(!HasMaxSerializedSize<std::string, S>);
    static_assert(!HasMaxSerializedSize<std::map<int, int>, S>);
    static_assert(!HasMaxSerializedSize<std::optional<std::vector<int>>, S>);
    static_assert(!HasMaxSerializedSize<std::variant<std::uint8_t, std::string>, S>);
    static_assert(!HasMaxSerializedSize<Test::serializer::WithVector, S>);
    static_assert(!HasMaxSerializedSize<Capped<std::vector<int>, 2>, S>);
    static_assert(HasMaxSerializedSize<std::uint8_t, S>);

    // A bound past size_t saturates to "none" instead of wrapping into a small number.
    constexpr std::size_t huge = std::numeric_limits<std::size_t>::max() / 4;
    static_assert(!HasMaxSerializedSize<Capped<std::uint64_t, huge>, std::uint64_t>);
    static_assert(!HasMaxSerializedSize<
                  std::tuple<Capped<std::uint8_t, huge * 3>, Capped<std::uint8_t, huge * 3>>,
                  std::uint64_t>);
}

TEST_CASE("A value at its largest serializes to max_serialized_size_v",
          "[serializer][max_serialized_size]") {
    using S = std::uint16_t;
    Test::serializer::WithCapped const v{
      .id     = 7,
      .values = {1, 2, 3},
      .flag   = std::uint8_t{4},
      .either = 2.5
    };

    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::serializer<Test::serializer::WithCapped, S>::serialize(v, ser));
    CHECK(buffer.size() == aglio::max_serialized_size_v<Test::serializer::WithCapped, S>);
}
