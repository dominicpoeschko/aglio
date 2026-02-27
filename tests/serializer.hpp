#pragma once

#include "types.hpp"

#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace Test::serializer {

template<typename Size_t,
         typename T>
bool round_trip(T const& in,
                T&       out) {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    if(!aglio::serializer<T, Size_t>::serialize(in, ser)) { return false; }
    aglio::DynamicDeserializationView de{buffer};
    return aglio::serializer<T, Size_t>::deserialize(out, de);
}

template<typename Size_t,
         typename T>
bool round_trip_check(T const& in) {
    T out{};
    if(!round_trip<Size_t>(in, out)) { return false; }
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

    CHECK(round_trip_check<S>(std::vector<int>{}));
    CHECK(round_trip_check<S>(std::map<int, int>{}));
    CHECK(round_trip_check<S>(std::set<int>{}));
    CHECK(round_trip_check<S>(std::string{}));
}

TEST_CASE("Serializer: range size overflow with small Size_t",
          "[serializer]") {
    std::vector<std::uint8_t> big(300, 0x42);

    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    bool ok = aglio::serializer<std::vector<std::uint8_t>, std::uint8_t>::serialize(big, ser);
    CHECK(!ok);
}

TEST_CASE("Serializer: variant out-of-bounds index",
          "[serializer]") {
    using V = std::variant<int, float>;
    using S = std::uint32_t;

    V                               v_in = 42;
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::serializer<V, S>::serialize(v_in, ser));

    buffer[0] = std::byte{5};

    V                                 v_out;
    aglio::DynamicDeserializationView de{buffer};
    CHECK(!aglio::serializer<V, S>::deserialize(v_out, de));
}

TEST_CASE("Serializer: deserialize from truncated buffer",
          "[serializer]") {
    using S = std::uint32_t;

    Types::Primitive                p_in = Types::createDefault<Types::Primitive>();
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    REQUIRE(aglio::serializer<Types::Primitive, S>::serialize(p_in, ser));

    buffer.resize(buffer.size() / 2);

    Types::Primitive                  p_out{};
    aglio::DynamicDeserializationView de{buffer};
    CHECK(!aglio::serializer<Types::Primitive, S>::deserialize(p_out, de));
}

TEST_CASE("Serializer: optional round-trip both states",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;

    CHECK(round_trip_check<S>(std::optional<int>{42}));
    CHECK(round_trip_check<S>(std::optional<int>{std::nullopt}));
}

TEST_CASE("Serializer: expected round-trip both states",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;
    using E = std::expected<int, std::string>;

    CHECK(round_trip_check<S>(E{42}));
    CHECK(round_trip_check<S>(E{std::unexpected{std::string("err")}}));
}

TEST_CASE("Serializer: variant round-trip each alternative",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;
    using V = std::variant<int, std::string, float>;

    CHECK(round_trip_check<S>(V{123}));
    CHECK(round_trip_check<S>(V{std::string("hello")}));
    CHECK(round_trip_check<S>(V{3.14f}));
}

TEST_CASE("Serializer: chrono duration round-trip",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;

    CHECK(round_trip_check<S>(std::chrono::milliseconds{12345}));
    CHECK(round_trip_check<S>(std::chrono::nanoseconds{9876543210LL}));
}

TEST_CASE("Serializer: nested containers with mixed optional",
          "[serializer]") {
    using namespace Test::serializer;
    using S = std::uint32_t;

    std::vector<std::optional<int>> v{1, std::nullopt, 3, std::nullopt, 5};
    CHECK(round_trip_check<S>(v));
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
    static_assert(aglio::has_fixed_serialized_size<std::array<int, 5>, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<std::array<std::string, 2>, std::uint32_t>);

    static_assert(aglio::has_fixed_serialized_size<std::uint32_t, std::uint32_t>);
    static_assert(
      aglio::has_fixed_serialized_size<Test::serializer::TwoFieldAggregate, std::uint32_t>);
    static_assert(aglio::has_fixed_serialized_size<Types::Empty, std::uint32_t>);
    static_assert(aglio::has_fixed_serialized_size<Types::Primitive, std::uint32_t>);
    static_assert(aglio::has_fixed_serialized_size<Types::Chrono, std::uint32_t>);
    static_assert(aglio::has_fixed_serialized_size<Types::Enum, std::uint32_t>);
    static_assert(aglio::has_fixed_serialized_size<std::chrono::milliseconds, std::uint32_t>);
    static_assert(aglio::has_fixed_serialized_size<std::pair<int, float>, std::uint32_t>);

    static_assert(!aglio::has_fixed_serialized_size<std::vector<int>, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<std::string, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<std::map<int, int>, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<std::set<int>, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<std::optional<int>, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<std::variant<int, float>, std::uint32_t>);
    static_assert(
      !aglio::has_fixed_serialized_size<std::expected<int, std::string>, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<Types::Container, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<Types::Nested, std::uint32_t>);
    static_assert(!aglio::has_fixed_serialized_size<std::pair<int, std::string>, std::uint32_t>);
    static_assert(
      !aglio::has_fixed_serialized_size<std::tuple<int, std::vector<int>>, std::uint32_t>);

    static_assert(serialized_size_v<std::uint32_t, std::uint8_t> == 4);
    static_assert(serialized_size_v<std::uint32_t, std::uint16_t> == 4);
    static_assert(serialized_size_v<Types::Primitive, std::uint8_t> == 43);
    static_assert(serialized_size_v<Types::Primitive, std::uint16_t> == 43);

    CHECK(true);
}

TEST_CASE("serialized_size_v matches actual serialized byte count",
          "[serializer][serialized_size]") {
    auto check = [](auto const& value) {
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

    Types::Primitive p_in = Types::createDefault<Types::Primitive>();

    std::ostringstream             oss;
    aglio::StreamSerializationView sser{oss};
    REQUIRE(aglio::serializer<Types::Primitive, S>::serialize(p_in, sser));

    std::string                      data = oss.str();
    std::istringstream               iss(data);
    aglio::StreamDeserializationView sde{iss};

    Types::Primitive p_out{};
    REQUIRE(aglio::serializer<Types::Primitive, S>::deserialize(p_out, sde));
    CHECK(p_in == p_out);
}

TEST_CASE("Serializer: stream deserialization truncated",
          "[serializer][stream]") {
    using S = std::uint32_t;

    std::istringstream               iss(std::string(2, '\0'));
    aglio::StreamDeserializationView sde{iss};

    Types::Primitive p_out{};
    CHECK(!aglio::serializer<Types::Primitive, S>::deserialize(p_out, sde));
}
