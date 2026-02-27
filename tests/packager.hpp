#pragma once

#include "types.hpp"

#include <aglio/packager.hpp>
#include <aglio/serialization_buffers.hpp>

namespace Test::packager {

struct MyCrc {
    using type = std::uint32_t;

    static type calc(std::span<std::byte const> data) {
        type crc = 0;
        for(auto b : data) { crc += static_cast<type>(b); }
        return crc;
    }
};

struct MsgId {
    std::uint8_t msg_type{};
    std::uint8_t channel{};
    bool         operator==(MsgId const&) const = default;
};

struct MsgIdCrc {
    using type = MsgId;

    static type calc(std::span<std::byte const> data) {
        type r{};
        for(auto b : data) {
            r.msg_type += static_cast<std::uint8_t>(b);
            r.channel += static_cast<std::uint8_t>(static_cast<unsigned>(b) * 3u);
        }
        return r;
    }
};

namespace Configs {

    struct Minimal {
        using Size_t = std::uint32_t;
    };

    struct SimplePackageStart {
        using Size_t                                = std::uint32_t;
        static constexpr std::uint16_t PackageStart = 0xABCD;
    };

    struct SimpleCrc {
        using Crc    = MyCrc;
        using Size_t = std::uint32_t;
    };

    struct CrcNoHeader {
        using Crc                          = MyCrc;
        using Size_t                       = std::uint32_t;
        static constexpr bool UseHeaderCrc = false;
    };

    struct Full {
        using Crc                                   = MyCrc;
        using Size_t                                = std::uint32_t;
        static constexpr std::uint16_t PackageStart = 0xABCD;
    };

    struct FullNoHeaderCrc {
        using Crc                                   = MyCrc;
        using Size_t                                = std::uint32_t;
        static constexpr std::uint16_t PackageStart = 0xABCD;
        static constexpr bool          UseHeaderCrc = false;
    };

    struct WithHeaderData {
        using Crc                                   = MyCrc;
        using Size_t                                = std::uint32_t;
        using HeaderData                            = std::uint8_t;
        static constexpr std::uint16_t PackageStart = 0xABCD;
    };

    struct WithDescribedHeaderData {
        using Crc                                   = MyCrc;
        using Size_t                                = std::uint32_t;
        using HeaderData                            = MsgId;
        static constexpr std::uint16_t PackageStart = 0xABCD;
    };

    struct WithDescribedCrc {
        using Crc                                   = MsgIdCrc;
        using Size_t                                = std::uint32_t;
        static constexpr std::uint16_t PackageStart = 0xABCD;
    };

}   // namespace Configs

template<typename T, typename TTuple>
struct product_one_trait;

template<typename T, typename... Us>
struct product_one_trait<T, std::tuple<Us...>> {
    using type = std::tuple<std::tuple<T, Us>...>;
};

template<typename TTuple1, typename TTuple2>
struct cartesian_product;

template<typename... Ts, typename... Us>
struct cartesian_product<std::tuple<Ts...>, std::tuple<Us...>> {
    using type = decltype(std::tuple_cat(
      std::declval<typename product_one_trait<Ts, std::tuple<Us...>>::type>()...));
};

using ConfigsList = std::tuple<Configs::Minimal,
                               Configs::SimplePackageStart,
                               Configs::SimpleCrc,
                               Configs::CrcNoHeader,
                               Configs::Full,
                               Configs::FullNoHeaderCrc>;

using TestCases = typename cartesian_product<Types::List, ConfigsList>::type;

template<typename Type,
         typename Packager>
void test() {
    std::vector<std::byte> buffer{};

    Type t_in = Types::createDefault<Type>();

    REQUIRE(Packager::pack(buffer, t_in));
    Type t_out{};
    auto result = Packager::unpack(buffer, t_out);

    REQUIRE(result.has_value());
    CHECK(buffer.size() == result->consumed);
    CHECK(t_in == t_out);
}

struct PacketHeader {
    std::uint32_t id{};
    std::uint8_t  typeId{};
    bool          operator==(PacketHeader const&) const = default;
};

struct SensorData {
    std::uint16_t temperature{};
    std::uint16_t humidity{};
    bool          operator==(SensorData const&) const = default;
};

struct CommandMsg {
    std::uint32_t command_code{};
    std::string   payload{};
    bool          operator==(CommandMsg const&) const = default;
};

struct StatusReport {
    std::uint8_t  status{};
    std::uint64_t uptime{};
    bool          operator==(StatusReport const&) const = default;
};

struct PacketHeaderConfig {
    using Crc                                   = MyCrc;
    using Size_t                                = std::uint32_t;
    using HeaderData                            = PacketHeader;
    static constexpr std::uint16_t PackageStart = 0xBEEF;
};

}   // namespace Test::packager

TEMPLATE_LIST_TEST_CASE("Packager",
                        "[cartesian]",
                        Test::packager::TestCases) {
    using Type   = std::tuple_element_t<0, TestType>;
    using Config = std::tuple_element_t<1, TestType>;

    Test::packager::test<Type, aglio::Packager<Config>>();
}

TEST_CASE("Serializer rejects range that exceeds fixed-capacity container max_size",
          "[serializer]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    std::vector<int>       src    = {1, 2, 3, 4, 5};
    std::vector<std::byte> buffer = {};
    REQUIRE(Packager::pack(buffer, src));

    std::array<int, 3> dst{};
    auto               result = Packager::unpack(buffer, dst);
    CHECK(!result.has_value());
}

TEST_CASE("Packager pair<primitive, struct ref>",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    Types::Primitive                  prim_in = Types::createDefault<Types::Primitive>();
    std::pair<int, Types::Primitive&> pair_in{42, prim_in};

    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, pair_in));

    Types::Primitive                  prim_out{};
    std::pair<int, Types::Primitive&> pair_out{0, prim_out};

    auto result = Packager::unpack(buffer, pair_out);

    REQUIRE(result.has_value());
    CHECK(buffer.size() == result->consumed);
    CHECK(pair_in.first == pair_out.first);
    CHECK(pair_in.second == pair_out.second);
}

TEST_CASE("Packager pair<primitive, variant<structs> ref>",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;
    using Variant  = std::variant<Types::Primitive, Types::Container, Types::Enum>;

    SECTION("Primitive variant") {
        Variant                  var_in = Types::createDefault<Types::Primitive>();
        std::pair<int, Variant&> pair_in{1, var_in};

        std::vector<std::byte> buffer{};
        REQUIRE(Packager::pack(buffer, pair_in));

        Variant                  var_out{};
        std::pair<int, Variant&> pair_out{0, var_out};

        auto result = Packager::unpack(buffer, pair_out);

        REQUIRE(result.has_value());
        CHECK(buffer.size() == result->consumed);
        CHECK(pair_in.first == pair_out.first);
        CHECK(pair_in.second == pair_out.second);
    }

    SECTION("Container variant") {
        Variant                  var_in = Types::createDefault<Types::Container>();
        std::pair<int, Variant&> pair_in{2, var_in};

        std::vector<std::byte> buffer{};
        REQUIRE(Packager::pack(buffer, pair_in));

        Variant                  var_out{};
        std::pair<int, Variant&> pair_out{0, var_out};

        auto result = Packager::unpack(buffer, pair_out);

        REQUIRE(result.has_value());
        CHECK(buffer.size() == result->consumed);
        CHECK(pair_in.first == pair_out.first);
        CHECK(pair_in.second == pair_out.second);
    }

    SECTION("Enum variant") {
        Variant                  var_in = Types::createDefault<Types::Enum>();
        std::pair<int, Variant&> pair_in{3, var_in};

        std::vector<std::byte> buffer{};
        REQUIRE(Packager::pack(buffer, pair_in));

        Variant                  var_out{};
        std::pair<int, Variant&> pair_out{0, var_out};

        auto result = Packager::unpack(buffer, pair_out);

        REQUIRE(result.has_value());
        CHECK(buffer.size() == result->consumed);
        CHECK(pair_in.first == pair_out.first);
        CHECK(pair_in.second == pair_out.second);
    }
}

TEST_CASE("HeaderData: round-trip preserves injected value",
          "[packager][headerinfo]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithHeaderData>;

    std::vector<std::byte> buffer{};
    int const              value_in = 1234;
    std::uint8_t const     info_in  = 42;

    REQUIRE(Packager::pack(buffer, value_in, info_in));

    int  value_out = 0;
    auto result    = Packager::unpack(buffer, value_out);

    REQUIRE(result.has_value());
    CHECK(result->header_data == info_in);
    CHECK(result->consumed == buffer.size());
    CHECK(value_out == value_in);
}

TEST_CASE("HeaderData: body corruption returns partial result with header_data",
          "[packager][headerinfo]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithHeaderData>;

    std::vector<std::byte> buffer{};
    int const              value_in = 5678;
    std::uint8_t const     info_in  = 7;

    REQUIRE(Packager::pack(buffer, value_in, info_in));

    buffer[11] ^= std::byte{0xFF};

    int  value_out = 0;
    auto result    = Packager::unpack(buffer, value_out);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == aglio::UnpackErrorKind::ParseFailure);
    CHECK(result.error().header_data == info_in);
    CHECK(result.error().consumed == buffer.size());
}

TEST_CASE("HeaderData: existing configs still return optional<size_t>",
          "[packager][headerinfo]") {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;

    std::vector<std::byte> buffer{};
    int const              value_in = 99;

    REQUIRE(Packager::pack(buffer, value_in));

    int  value_out = 0;
    auto result    = Packager::unpack(buffer, value_out);

    static_assert(std::is_same_v<decltype(result),
                                 std::expected<Packager::UnpackSuccess, Packager::UnpackError>>);
    REQUIRE(result.has_value());
    CHECK(result->consumed == buffer.size());
    CHECK(value_out == value_in);
}

TEST_CASE("WithDescribedHeaderData: round-trip preserves MsgId header_data",
          "[packager][headerinfo][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedHeaderData>;

    std::vector<std::byte>      buffer{};
    int const                   value_in = 4321;
    Test::packager::MsgId const info_in{.msg_type = 7, .channel = 3};

    REQUIRE(Packager::pack(buffer, value_in, info_in));

    int  value_out = 0;
    auto result    = Packager::unpack(buffer, value_out);

    REQUIRE(result.has_value());
    CHECK(result->header_data.msg_type == info_in.msg_type);
    CHECK(result->header_data.channel == info_in.channel);
    CHECK(result->consumed == buffer.size());
    CHECK(value_out == value_in);
}

TEST_CASE("WithDescribedHeaderData: body corruption returns partial result with MsgId header_data",
          "[packager][headerinfo][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedHeaderData>;

    std::vector<std::byte>      buffer{};
    int const                   value_in = 8765;
    Test::packager::MsgId const info_in{.msg_type = 11, .channel = 5};

    REQUIRE(Packager::pack(buffer, value_in, info_in));

    buffer[12] ^= std::byte{0xFF};

    int  value_out = 0;
    auto result    = Packager::unpack(buffer, value_out);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == aglio::UnpackErrorKind::ParseFailure);
    CHECK(result.error().header_data.msg_type == info_in.msg_type);
    CHECK(result.error().header_data.channel == info_in.channel);
    CHECK(result.error().consumed == buffer.size());
}

TEST_CASE("WithDescribedCrc: round-trip succeeds with MsgId as Crc::type",
          "[packager][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedCrc>;

    std::vector<std::byte> buffer{};
    int const              value_in = 99;

    REQUIRE(Packager::pack(buffer, value_in));

    int  value_out = 0;
    auto result    = Packager::unpack(buffer, value_out);

    REQUIRE(result.has_value());
    CHECK(result->consumed == buffer.size());
    CHECK(value_out == value_in);
}

TEST_CASE("pack fails gracefully when output buffer max_size is exceeded",
          "[packager]") {
    struct BoundedBuffer {
        std::array<std::byte, 8> storage{};
        std::size_t              sz{0};

        std::byte* data() { return storage.data(); }

        std::size_t size() const { return sz; }

        std::size_t max_size() const { return storage.size(); }

        void resize(std::size_t n) { sz = n; }
    };

    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    BoundedBuffer buf{};
    CHECK(Packager::pack(buf, int{42}));

    BoundedBuffer buf2{};
    CHECK(!Packager::pack(buf2, std::string{"hello"}));
}

TEST_CASE("Packager: multiple messages in one buffer",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, int{111}));
    REQUIRE(Packager::pack(buffer, int{222}));

    int  first  = 0;
    auto result = Packager::unpack(buffer, first);
    REQUIRE(result.has_value());
    CHECK(first == 111);

    auto remaining = std::span{buffer}.subspan(result->consumed);
    int  second    = 0;
    auto result2   = Packager::unpack(remaining, second);
    REQUIRE(result2.has_value());
    CHECK(second == 222);
}

TEST_CASE("Packager: truncated message returns nullopt",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, int{42}));

    buffer.resize(buffer.size() - 2);

    int  out    = 0;
    auto result = Packager::unpack(buffer, out);
    CHECK(!result.has_value());
}

TEST_CASE("Packager: bodySize smaller than CrcSize does not underflow",
          "[packager]") {
    SECTION("CrcNoHeader") {
        using Packager = aglio::Packager<Test::packager::Configs::CrcNoHeader>;

        std::vector<std::byte> buffer(8, std::byte{0});

        int  out    = 0;
        auto result = Packager::unpack(buffer, out);
        CHECK(!result.has_value());
    }

    SECTION("FullNoHeaderCrc") {
        using Packager = aglio::Packager<Test::packager::Configs::FullNoHeaderCrc>;

        std::vector<std::byte> buffer(10, std::byte{0});
        std::uint16_t const    pkg_start = 0xABCD;
        std::memcpy(buffer.data(), &pkg_start, sizeof(pkg_start));

        int  out    = 0;
        auto result = Packager::unpack(buffer, out);
        CHECK(!result.has_value());
    }

    SECTION("CrcNoHeader with nonzero bodySize still below CrcSize") {
        using Packager = aglio::Packager<Test::packager::Configs::CrcNoHeader>;

        for(std::uint32_t bad_size : {1u, 2u, 3u}) {
            std::vector<std::byte> buffer(4 + bad_size, std::byte{0});
            std::memcpy(buffer.data(), &bad_size, sizeof(bad_size));

            int  out    = 0;
            auto result = Packager::unpack(buffer, out);
            CHECK(!result.has_value());
        }
    }
}

TEST_CASE("Packager: PackageStart byte pattern in body",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;

    std::vector<std::uint8_t> payload = {0xCD, 0xAB, 0xCD, 0xAB};

    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, payload));

    std::vector<std::uint8_t> out;
    auto                      result = Packager::unpack(buffer, out);
    REQUIRE(result.has_value());
    CHECK(out == payload);
}

struct SmallMaxConfig {
    using Size_t                                = std::uint32_t;
    static constexpr std::uint32_t MaxSize      = 4;
    static constexpr std::uint16_t PackageStart = 0xABCD;
};

TEST_CASE("Packager: MaxSize exceeded rejects oversized body",
          "[packager]") {
    using Packager = aglio::Packager<SmallMaxConfig>;

    std::vector<std::byte> buffer{};
    Packager::pack(buffer, std::string{"hello world"});

    std::string out;
    auto        result = Packager::unpack(buffer, out);
    CHECK(!result.has_value());
}

TEST_CASE("Packager: garbage prefix with valid message after",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;

    std::vector<std::byte> valid_buf{};
    REQUIRE(Packager::pack(valid_buf, int{77}));

    std::vector<std::byte> buffer{};
    buffer.push_back(std::byte{0xCD});
    buffer.push_back(std::byte{0xAB});
    for(int i = 0; i < 20; ++i) { buffer.push_back(std::byte{0x00}); }
    buffer.insert(buffer.end(), valid_buf.begin(), valid_buf.end());

    int  out    = 0;
    auto result = Packager::unpack(buffer, out);
    REQUIRE(result.has_value());
    CHECK(out == 77);
}

TEST_CASE("Packager: empty struct round-trip with CRC",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;

    Types::Empty           empty_in{};
    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, empty_in));

    Types::Empty empty_out{};
    auto         result = Packager::unpack(buffer, empty_out);
    REQUIRE(result.has_value());
    CHECK(result->consumed == buffer.size());
    CHECK(empty_in == empty_out);
}

TEST_CASE("validate: type-dispatched unpacking via HeaderData",
          "[packager][validate]") {
    using Packager = aglio::Packager<Test::packager::PacketHeaderConfig>;

    SECTION("SensorData (typeId=1)") {
        std::vector<std::byte>             buffer{};
        Test::packager::SensorData const   data_in{.temperature = 235, .humidity = 650};
        Test::packager::PacketHeader const hdr{.id = 100, .typeId = 1};

        REQUIRE(Packager::pack(buffer, data_in, hdr));

        auto pkg = Packager::validate(buffer);
        REQUIRE(pkg.has_value());
        CHECK(pkg->header_data == hdr);
        CHECK(pkg->consumed == buffer.size());

        auto                              body = pkg->body;
        aglio::DynamicDeserializationView debuff{body};
        Test::packager::SensorData        data_out{};
        REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, data_out));
        CHECK(data_out == data_in);
    }

    SECTION("CommandMsg (typeId=2)") {
        std::vector<std::byte>             buffer{};
        Test::packager::CommandMsg const   msg_in{.command_code = 0xDEAD, .payload = "reboot"};
        Test::packager::PacketHeader const hdr{.id = 200, .typeId = 2};

        REQUIRE(Packager::pack(buffer, msg_in, hdr));

        auto pkg = Packager::validate(buffer);
        REQUIRE(pkg.has_value());
        CHECK(pkg->header_data == hdr);
        CHECK(pkg->consumed == buffer.size());

        auto                              body = pkg->body;
        aglio::DynamicDeserializationView debuff{body};
        Test::packager::CommandMsg        msg_out{};
        REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, msg_out));
        CHECK(msg_out == msg_in);
    }

    SECTION("StatusReport (typeId=3)") {
        std::vector<std::byte>             buffer{};
        Test::packager::StatusReport const report_in{.status = 1, .uptime = 86400};
        Test::packager::PacketHeader const hdr{.id = 300, .typeId = 3};

        REQUIRE(Packager::pack(buffer, report_in, hdr));

        auto pkg = Packager::validate(buffer);
        REQUIRE(pkg.has_value());
        CHECK(pkg->header_data == hdr);
        CHECK(pkg->consumed == buffer.size());

        auto                              body = pkg->body;
        aglio::DynamicDeserializationView debuff{body};
        Test::packager::StatusReport      report_out{};
        REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, report_out));
        CHECK(report_out == report_in);
    }

    SECTION("dispatch via typeId switch") {
        std::vector<std::byte> buffer{};

        Test::packager::SensorData const   sensor{.temperature = 100, .humidity = 500};
        Test::packager::CommandMsg const   cmd{.command_code = 42, .payload = "hello"};
        Test::packager::StatusReport const status{.status = 2, .uptime = 1000};

        REQUIRE(Packager::pack(buffer, sensor, {.id = 1, .typeId = 1}));
        REQUIRE(Packager::pack(buffer, cmd, {.id = 2, .typeId = 2}));
        REQUIRE(Packager::pack(buffer, status, {.id = 3, .typeId = 3}));

        auto remaining = std::span{buffer};
        int  count     = 0;

        for(auto pkg = Packager::validate(remaining); pkg.has_value();
            pkg      = Packager::validate(remaining))
        {
            auto                              body = pkg->body;
            aglio::DynamicDeserializationView debuff{body};

            switch(pkg->header_data.typeId) {
            case 1:
                {
                    Test::packager::SensorData out{};
                    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, out));
                    CHECK(out == sensor);
                    CHECK(pkg->header_data.id == 1);
                }
                break;
            case 2:
                {
                    Test::packager::CommandMsg out{};
                    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, out));
                    CHECK(out == cmd);
                    CHECK(pkg->header_data.id == 2);
                }
                break;
            case 3:
                {
                    Test::packager::StatusReport out{};
                    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, out));
                    CHECK(out == status);
                    CHECK(pkg->header_data.id == 3);
                }
                break;
            default: FAIL("unexpected typeId");
            }

            remaining = remaining.subspan(pkg->consumed);
            ++count;
        }

        CHECK(count == 3);
    }
}

TEST_CASE("validate: round-trip preserves header info and body bytes",
          "[packager][validate]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithHeaderData>;

    std::vector<std::byte> buffer{};
    int const              value_in = 1234;
    std::uint8_t const     info_in  = 42;

    REQUIRE(Packager::pack(buffer, value_in, info_in));

    auto pkg = Packager::validate(buffer);
    REQUIRE(pkg.has_value());
    CHECK(pkg->header_data == info_in);
    CHECK(pkg->consumed == buffer.size());
    CHECK(!pkg->body.empty());

    auto                              body_copy = pkg->body;
    int                               value_out = 0;
    aglio::DynamicDeserializationView debuff{body_copy};
    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, value_out));
    CHECK(value_out == value_in);
}

TEST_CASE("validate: corrupted package returns nullopt",
          "[packager][validate]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithHeaderData>;

    std::vector<std::byte> buffer{};
    int const              value_in = 5678;
    std::uint8_t const     info_in  = 7;

    REQUIRE(Packager::pack(buffer, value_in, info_in));

    buffer[11] ^= std::byte{0xFF};

    auto pkg = Packager::validate(buffer);
    CHECK(!pkg.has_value());
    CHECK(pkg.error().kind == aglio::UnpackErrorKind::ParseFailure);
}

TEST_CASE("validate: with described HeaderData",
          "[packager][validate][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedHeaderData>;

    std::vector<std::byte>      buffer{};
    int const                   value_in = 4321;
    Test::packager::MsgId const info_in{.msg_type = 7, .channel = 3};

    REQUIRE(Packager::pack(buffer, value_in, info_in));

    auto pkg = Packager::validate(buffer);
    REQUIRE(pkg.has_value());
    CHECK(pkg->header_data.msg_type == info_in.msg_type);
    CHECK(pkg->header_data.channel == info_in.channel);
    CHECK(pkg->consumed == buffer.size());
}
