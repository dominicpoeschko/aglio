#pragma once

#include "types.hpp"

#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
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

TEST_CASE("Serializer: serialized_size_v for chrono",
          "[serializer][serialized_size]") {
    static_assert(aglio::serialized_size_v<std::chrono::milliseconds, std::uint32_t>
                  == sizeof(std::int64_t));
    CHECK(true);
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
