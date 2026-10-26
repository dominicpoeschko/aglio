#pragma once

#include "packager_configs.hpp"
#include "types.hpp"

#include <aglio/packager.hpp>
#include <aglio/serialization_buffers.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

namespace Test::packager {

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

    Type tIn = Types::createDefault<Type>();

    REQUIRE(Packager::pack(buffer, tIn));
    Type tOut{};
    auto result = Packager::unpack(buffer, tOut);

    REQUIRE(result.has_value());
    CHECK(buffer.size() == result->consumed);
    CHECK(tIn == tOut);
}

/// A buffer whose max_size() is its capacity, known at compile time.
template<std::size_t N>
struct FixedBuffer {
    std::array<std::byte, N> storage{};
    std::size_t              used{};

    static constexpr std::size_t capacity() { return N; }

    static constexpr std::size_t max_size() { return N; }

    [[nodiscard]] std::size_t size() const { return used; }

    void resize(std::size_t n) {
        REQUIRE(n <= N);
        for(std::size_t i = used; i < n; ++i) { storage[i] = std::byte{}; }
        used = n;
    }

    std::span<std::byte> bytes() { return std::span{storage}.first(used); }

    [[nodiscard]] std::span<std::byte const> bytes() const {
        return std::span{storage}.first(used);
    }

    std::byte* data() AGLIO_LIFETIMEBOUND { return storage.data(); }

    [[nodiscard]] std::byte const* data() const { return storage.data(); }

    auto begin() { return bytes().begin(); }

    auto end() { return bytes().end(); }

    [[nodiscard]] auto begin() const { return bytes().begin(); }

    [[nodiscard]] auto end() const { return bytes().end(); }

    std::byte& operator[](std::size_t i) AGLIO_LIFETIMEBOUND { return storage[i]; }
};

/// The same with std::string's resize_and_overwrite. Growing it by resize() is an error.
template<std::size_t N>
struct RawFixedBuffer : FixedBuffer<N> {
    void resize(std::size_t n) {
        REQUIRE(n <= this->used);
        this->used = n;
    }

    template<typename Op>
    void resize_and_overwrite(std::size_t n,
                              Op&&        op) {
        REQUIRE(n <= N);
        std::size_t const kept = op(this->storage.data(), n);
        REQUIRE(kept <= n);
        this->used = kept;
    }
};

/// A fixed-capacity buffer with no begin()/end(): data() and size() are all the window needs.
template<std::size_t N>
struct BareFixedBuffer {
    std::array<std::byte, N> storage{};
    std::size_t              used{};

    static constexpr std::size_t capacity() { return N; }

    static constexpr std::size_t max_size() { return N; }

    [[nodiscard]] std::size_t size() const { return used; }

    void resize(std::size_t n) {
        REQUIRE(n <= N);
        used = n;
    }

    std::byte* data() AGLIO_LIFETIMEBOUND { return storage.data(); }
};

static_assert(aglio::detail::fixed_capacity_v<FixedBuffer<8>>);
static_assert(!aglio::detail::fixed_capacity_v<std::vector<std::byte>>);
static_assert(aglio::detail::SizedOnce<FixedBuffer<8>>);
static_assert(aglio::detail::SizedOnce<RawFixedBuffer<8>>);
static_assert(aglio::detail::SizedOnce<BareFixedBuffer<8>>);
static_assert(!aglio::detail::Overwritable<FixedBuffer<8>>);
static_assert(aglio::detail::Overwritable<RawFixedBuffer<8>>);
static_assert(!aglio::detail::SizedOnce<std::vector<std::byte>>);

/// Packing into a fixed buffer (serialized into one window) gives the bytes packing into a
/// std::vector (grown per field) gives - behind bytes already in the buffer too.
template<typename Type,
         typename Packager,
         typename Fixed>
void testFixed() {
    Type const tIn = Types::createDefault<Type>();

    std::vector<std::byte> want{};
    REQUIRE(Packager::pack(want, tIn));

    Fixed fixed{};
    REQUIRE(Packager::pack(fixed, tIn));
    CHECK(std::ranges::equal(fixed, want));

    Fixed behind{};
    behind.used = 3;
    behind[0]   = std::byte{0xA1};
    behind[2]   = std::byte{0xC3};
    REQUIRE(Packager::pack(behind, tIn));
    REQUIRE(behind.size() == 3 + want.size());
    CHECK(behind[0] == std::byte{0xA1});
    CHECK(behind[2] == std::byte{0xC3});
    CHECK(std::ranges::equal(behind.bytes().subspan(3), want));

    Type                   tOut{};
    std::vector<std::byte> packed(fixed.bytes().begin(), fixed.bytes().end());
    auto const             result = Packager::unpack(packed, tOut);
    REQUIRE(result.has_value());
    CHECK(tIn == tOut);
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
    std::uint32_t commandCode{};
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

TEMPLATE_LIST_TEST_CASE("Packager into a fixed-capacity buffer",
                        "[cartesian][fixed]",
                        Test::packager::TestCases) {
    using Type   = std::tuple_element_t<0, TestType>;
    using Config = std::tuple_element_t<1, TestType>;

    Test::packager::testFixed<Type, aglio::Packager<Config>, Test::packager::FixedBuffer<8192>>();
    Test::packager::
      testFixed<Type, aglio::Packager<Config>, Test::packager::RawFixedBuffer<8192>>();
}

namespace Test::packager {
using SmallFixedBuffers = std::tuple<FixedBuffer<12>, RawFixedBuffer<12>>;
}

TEMPLATE_LIST_TEST_CASE("A pack that fails leaves the buffer as it was",
                        "[packager][fixed]",
                        Test::packager::SmallFixedBuffers) {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;
    std::vector<std::uint32_t> const value{1, 2, 3, 4, 5, 6, 7, 8};

    // No room for the body, and none for the header either.
    for(std::size_t const prefix : std::array{std::size_t{2}, std::size_t{11}}) {
        TestType buffer{};
        buffer.used = prefix;
        buffer[0]   = std::byte{0xA1};
        CHECK(!Packager::pack(buffer, value));
        CHECK(buffer.size() == prefix);
        CHECK(buffer[0] == std::byte{0xA1});
    }

    std::vector<std::byte> grown{std::byte{0xA1}, std::byte{0xB2}};
    // Fails on MaxSize, after the header went in.
    CHECK(!aglio::Packager<Test::packager::Configs::SmallMax>::pack(grown, std::uint64_t{1}));
    CHECK(std::ranges::equal(grown, std::array{std::byte{0xA1}, std::byte{0xB2}}));
}

TEST_CASE("Packager into a fixed buffer that has no begin()",
          "[packager][fixed]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;
    std::vector<std::uint32_t> const value{1, 2, 3};

    std::vector<std::byte> want{};
    REQUIRE(Packager::pack(want, value));

    Test::packager::BareFixedBuffer<64> bare{};
    REQUIRE(Packager::pack(bare, value));
    REQUIRE(bare.size() == want.size());
    CHECK(std::ranges::equal(std::span{bare.storage}.first(bare.size()), want));
}

namespace Test::packager {
struct WithEmptyArray {
    std::uint16_t      before{};
    std::array<int, 0> nothing{};
    std::uint16_t      after{};
    bool               operator==(WithEmptyArray const&) const = default;
};
}   // namespace Test::packager

TEST_CASE("A zero-length array field takes no bytes beyond its size, in either kind of buffer",
          "[serializer][fixed]") {
    using Serializer = aglio::detail::Serializer<std::uint16_t>;
    Test::packager::WithEmptyArray const value{.before = 0x1122, .nothing = {}, .after = 0x3344};

    std::vector<std::byte> want{};
    REQUIRE(Serializer::serialize(want, value));
    CHECK(want.size() == 2 + 2 + 2);

    Test::packager::FixedBuffer<16> fixed{};
    REQUIRE(Serializer::serialize(fixed, value));
    CHECK(std::ranges::equal(fixed, want));

    Test::packager::WithEmptyArray out{};
    auto const                     ec = Serializer::deserialize(want, out);
    CHECK(!ec);
    CHECK(out == value);
}

namespace Test::packager {
struct Short {
    std::uint8_t value{};
};

struct HoldsShort {
    std::uint16_t before{};
    Short         wrong{};
};

struct Plain {
    std::uint8_t                 a{};
    std::uint32_t                b{};
    std::array<std::uint16_t, 3> c{};
    bool                         d{};
    bool                         operator==(Plain const&) const = default;
};
}   // namespace Test::packager

// Writes one byte and claims to take two.
template<typename SizeT>
struct aglio::serializer<Test::packager::Short, SizeT> {
    static bool serialize(Test::packager::Short const& v,
                          auto&                        buffer) {
        return aglio::serializer<std::uint8_t, SizeT>::serialize(v.value, buffer);
    }

    static bool deserialize(Test::packager::Short& v,
                            auto&                  buffer) {
        return aglio::serializer<std::uint8_t, SizeT>::deserialize(v.value, buffer);
    }
};

template<typename SizeT>
struct aglio::serialized_size<Test::packager::Short, SizeT>
  : std::integral_constant<std::size_t, 2> {};

TEST_CASE("A value of fixed serialized size takes one claim of the buffer",
          "[serializer][fixed]") {
    using Serializer = aglio::detail::Serializer<std::uint16_t>;
    using Test::packager::Plain;
    constexpr std::size_t Size = aglio::serialized_size_v<Plain, std::uint16_t>;
    REQUIRE(Size == 1 + 4 + (2 + 3 * 2) + 1);

    Plain const value{
      .a = 0x11,
      .b = 0x22334455,
      .c = {1, 2, 3},
      .d = true
    };

    std::vector<std::byte> want{};
    REQUIRE(Serializer::serialize(want, value));
    REQUIRE(want.size() == Size);
    CHECK(want[0] == std::byte{0x11});
    CHECK(want[1] == std::byte{0x55});
    CHECK(want[4] == std::byte{0x22});

    // One byte short, exactly enough, and with room to spare.
    Test::packager::RawFixedBuffer<1 + Size - 1> small{};
    small.used = 1;
    CHECK(!Serializer::serialize(small, value));
    CHECK(small.size() == 1);

    Test::packager::FixedBuffer<1 + Size> exact{};
    exact.used = 1;
    exact[0]   = std::byte{0xEE};
    REQUIRE(Serializer::serialize(exact, value));
    CHECK(exact[0] == std::byte{0xEE});
    CHECK(std::ranges::equal(exact.bytes().subspan(1), want));

    Plain      out{};
    auto const ec = Serializer::deserialize(want, out);
    CHECK(!ec);
    CHECK(ec.location == Size);
    CHECK(out == value);

    // Fewer bytes than the value takes: refused as a whole.
    auto cut = std::span{want}.first(Size - 1);
    CHECK(Serializer::deserialize(cut, out));
}

TEST_CASE("A serialized_size that is not what the serializer writes fails instead of leaving a gap",
          "[serializer][fixed]") {
    using Serializer = aglio::detail::Serializer<std::uint16_t>;
    static_assert(aglio::serialized_size_v<Test::packager::HoldsShort, std::uint16_t> == 4);
    Test::packager::HoldsShort const value{.before = 7, .wrong = {.value = 9}};

    std::vector<std::byte> grown{};
    CHECK(!Serializer::serialize(grown, value));

    Test::packager::FixedBuffer<16> fixed{};
    CHECK(!Serializer::serialize(fixed, value));
    CHECK(fixed.size() == 0);

    std::array<std::byte, 4> const bytes{};
    Test::packager::HoldsShort     out{};
    auto                           in = std::span{bytes};
    CHECK(Serializer::deserialize(in, out));
}

TEST_CASE("Serializer into a fixed buffer too small for the value fails and writes nothing",
          "[serializer][fixed]") {
    using Serializer = aglio::detail::Serializer<std::uint16_t>;
    std::vector<std::uint32_t> const value{1, 2, 3, 4, 5, 6, 7, 8};

    Test::packager::FixedBuffer<64> roomy{};
    roomy.resize(2);
    REQUIRE(Serializer::serialize(roomy, value));
    std::vector<std::byte> want{};
    REQUIRE(Serializer::serialize(want, value));
    CHECK(roomy.size() == 2 + want.size());
    CHECK(std::ranges::equal(roomy.bytes().subspan(2), want));

    Test::packager::FixedBuffer<16> small{};
    small.resize(2);
    CHECK(!Serializer::serialize(small, value));
    CHECK(small.size() == 2);

    // Exactly full still fits.
    Test::packager::FixedBuffer<2 + 2 + 8 * 4> exact{};
    exact.resize(2);
    REQUIRE(want.size() == 2 + 8 * 4);
    CHECK(Serializer::serialize(exact, value));
    CHECK(exact.size() == exact.capacity());
}

TEST_CASE("Serializer rejects range that exceeds fixed-capacity container max_size",
          "[serializer]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    std::vector<int> const src    = {1, 2, 3, 4, 5};
    std::vector<std::byte> buffer = {};
    REQUIRE(Packager::pack(buffer, src));

    std::array<int, 3> dst{};
    auto const         result = Packager::unpack(buffer, dst);
    CHECK(!result.has_value());
}

TEST_CASE("Packager pair<primitive, struct ref>",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    Types::Primitive                  primIn = Types::createDefault<Types::Primitive>();
    std::pair<int, Types::Primitive&> pairIn{42, std::ref(primIn)};

    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, pairIn));

    Types::Primitive                  primOut{};
    std::pair<int, Types::Primitive&> pairOut{0, std::ref(primOut)};

    auto result = Packager::unpack(buffer, pairOut);

    REQUIRE(result.has_value());
    CHECK(buffer.size() == result->consumed);
    CHECK(pairIn.first == pairOut.first);
    CHECK(pairIn.second == pairOut.second);
}

TEST_CASE("Packager pair<primitive, variant<structs> ref>",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;
    using Variant  = std::variant<Types::Primitive, Types::Container, Types::Enum>;

    SECTION("Primitive variant") {
        Variant                  varIn = Types::createDefault<Types::Primitive>();
        std::pair<int, Variant&> pairIn{1, std::ref(varIn)};

        std::vector<std::byte> buffer{};
        REQUIRE(Packager::pack(buffer, pairIn));

        Variant                  varOut{};
        std::pair<int, Variant&> pairOut{0, std::ref(varOut)};

        auto result = Packager::unpack(buffer, pairOut);

        REQUIRE(result.has_value());
        CHECK(buffer.size() == result->consumed);
        CHECK(pairIn.first == pairOut.first);
        CHECK(pairIn.second == pairOut.second);
    }

    SECTION("Container variant") {
        Variant                  varIn = Types::createDefault<Types::Container>();
        std::pair<int, Variant&> pairIn{2, std::ref(varIn)};

        std::vector<std::byte> buffer{};
        REQUIRE(Packager::pack(buffer, pairIn));

        Variant                  varOut{};
        std::pair<int, Variant&> pairOut{0, std::ref(varOut)};

        auto result = Packager::unpack(buffer, pairOut);

        REQUIRE(result.has_value());
        CHECK(buffer.size() == result->consumed);
        CHECK(pairIn.first == pairOut.first);
        CHECK(pairIn.second == pairOut.second);
    }

    SECTION("Enum variant") {
        Variant                  varIn = Types::createDefault<Types::Enum>();
        std::pair<int, Variant&> pairIn{3, std::ref(varIn)};

        std::vector<std::byte> buffer{};
        REQUIRE(Packager::pack(buffer, pairIn));

        Variant                  varOut{};
        std::pair<int, Variant&> pairOut{0, std::ref(varOut)};

        auto result = Packager::unpack(buffer, pairOut);

        REQUIRE(result.has_value());
        CHECK(buffer.size() == result->consumed);
        CHECK(pairIn.first == pairOut.first);
        CHECK(pairIn.second == pairOut.second);
    }
}

TEST_CASE("HeaderData: round-trip preserves injected value",
          "[packager][headerinfo]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithHeaderData>;

    std::vector<std::byte> buffer{};
    int const              valueIn = 1234;
    std::uint8_t const     infoIn  = 42;

    REQUIRE(Packager::pack(buffer, valueIn, infoIn));

    int  valueOut = 0;
    auto result   = Packager::unpack(buffer, valueOut);

    REQUIRE(result.has_value());
    CHECK(result->headerData == infoIn);
    CHECK(result->consumed == buffer.size());
    CHECK(valueOut == valueIn);
}

TEST_CASE("HeaderData: body corruption returns partial result with header_data",
          "[packager][headerinfo]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithHeaderData>;

    std::vector<std::byte> buffer{};
    int const              valueIn = 5678;
    std::uint8_t const     infoIn  = 7;

    REQUIRE(Packager::pack(buffer, valueIn, infoIn));

    buffer[11] ^= std::byte{0xFF};

    int  valueOut = 0;
    auto result   = Packager::unpack(buffer, valueOut);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == aglio::UnpackErrorKind::ParseFailure);
    CHECK(result.error().headerData == infoIn);
    CHECK(result.error().consumed == buffer.size());
}

TEST_CASE("HeaderData: existing configs still return optional<size_t>",
          "[packager][headerinfo]") {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;

    std::vector<std::byte> buffer{};
    int const              valueIn = 99;

    REQUIRE(Packager::pack(buffer, valueIn));

    int  valueOut = 0;
    auto result   = Packager::unpack(buffer, valueOut);

    static_assert(std::is_same_v<decltype(result),
                                 std::expected<Packager::UnpackSuccess, Packager::UnpackError>>);
    REQUIRE(result.has_value());
    CHECK(result->consumed == buffer.size());
    CHECK(valueOut == valueIn);
}

TEST_CASE("WithDescribedHeaderData: round-trip preserves MsgId header_data",
          "[packager][headerinfo][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedHeaderData>;

    std::vector<std::byte>      buffer{};
    int const                   valueIn = 4321;
    Test::packager::MsgId const infoIn{.msgType = 7, .channel = 3};

    REQUIRE(Packager::pack(buffer, valueIn, infoIn));

    int  valueOut = 0;
    auto result   = Packager::unpack(buffer, valueOut);

    REQUIRE(result.has_value());
    CHECK(result->headerData.msgType == infoIn.msgType);
    CHECK(result->headerData.channel == infoIn.channel);
    CHECK(result->consumed == buffer.size());
    CHECK(valueOut == valueIn);
}

TEST_CASE("WithDescribedHeaderData: body corruption returns partial result with MsgId header_data",
          "[packager][headerinfo][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedHeaderData>;

    std::vector<std::byte>      buffer{};
    int const                   valueIn = 8765;
    Test::packager::MsgId const infoIn{.msgType = 11, .channel = 5};

    REQUIRE(Packager::pack(buffer, valueIn, infoIn));

    buffer[12] ^= std::byte{0xFF};

    int  valueOut = 0;
    auto result   = Packager::unpack(buffer, valueOut);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == aglio::UnpackErrorKind::ParseFailure);
    CHECK(result.error().headerData.msgType == infoIn.msgType);
    CHECK(result.error().headerData.channel == infoIn.channel);
    CHECK(result.error().consumed == buffer.size());
}

TEST_CASE("WithDescribedCrc: round-trip succeeds with MsgId as Crc::type",
          "[packager][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedCrc>;

    std::vector<std::byte> buffer{};
    int const              valueIn = 99;

    REQUIRE(Packager::pack(buffer, valueIn));

    int  valueOut = 0;
    auto result   = Packager::unpack(buffer, valueOut);

    REQUIRE(result.has_value());
    CHECK(result->consumed == buffer.size());
    CHECK(valueOut == valueIn);
}

TEST_CASE("pack fails gracefully when output buffer max_size is exceeded",
          "[packager]") {
    struct BoundedBuffer {
        std::array<std::byte, 8> storage{};
        std::size_t              sz{0};

        std::byte* data() AGLIO_LIFETIMEBOUND { return storage.data(); }

        [[nodiscard]] std::size_t size() const { return sz; }

        [[nodiscard]] std::size_t max_size() const { return storage.size(); }

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

    auto       remaining = std::span{buffer}.subspan(result->consumed);
    int        second    = 0;
    auto const result2   = Packager::unpack(remaining, second);
    REQUIRE(result2.has_value());
    CHECK(second == 222);
}

TEST_CASE("Packager: truncated message returns nullopt",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Minimal>;

    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, int{42}));

    buffer.resize(buffer.size() - 2);

    int        out    = 0;
    auto const result = Packager::unpack(buffer, out);
    CHECK(!result.has_value());
}

TEST_CASE("Packager: bodySize smaller than CrcSize does not underflow",
          "[packager]") {
    SECTION("CrcNoHeader") {
        using Packager = aglio::Packager<Test::packager::Configs::CrcNoHeader>;

        std::vector<std::byte> buffer(8, std::byte{0});

        int        out    = 0;
        auto const result = Packager::unpack(buffer, out);
        CHECK(!result.has_value());
    }

    SECTION("FullNoHeaderCrc") {
        using Packager = aglio::Packager<Test::packager::Configs::FullNoHeaderCrc>;

        std::vector<std::byte> buffer(10, std::byte{0});
        std::uint16_t const    pkgStart = 0xABCD;
        std::memcpy(buffer.data(), &pkgStart, sizeof(pkgStart));

        int        out    = 0;
        auto const result = Packager::unpack(buffer, out);
        CHECK(!result.has_value());
    }

    SECTION("CrcNoHeader with nonzero bodySize still below CrcSize") {
        using Packager = aglio::Packager<Test::packager::Configs::CrcNoHeader>;

        for(std::uint32_t badSize : {1U, 2U, 3U}) {
            std::vector<std::byte> buffer(4 + badSize, std::byte{0});
            std::memcpy(buffer.data(), &badSize, sizeof(badSize));

            int        out    = 0;
            auto const result = Packager::unpack(buffer, out);
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
    auto const                result = Packager::unpack(buffer, out);
    REQUIRE(result.has_value());
    CHECK(out == payload);
}

TEST_CASE("Packager: MaxSize exceeded rejects oversized body",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::SmallMax>;

    std::vector<std::byte> buffer{};
    Packager::pack(buffer, std::string{"hello world"});

    std::string out;
    auto const  result = Packager::unpack(buffer, out);
    CHECK(!result.has_value());
}

TEST_CASE("Packager: garbage prefix with valid message after",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;

    std::vector<std::byte> validBuf{};
    REQUIRE(Packager::pack(validBuf, int{77}));

    std::vector<std::byte> buffer{};
    buffer.push_back(std::byte{0xCD});
    buffer.push_back(std::byte{0xAB});
    for(int i = 0; i < 20; ++i) { buffer.push_back(std::byte{0x00}); }
    buffer.insert(buffer.end(), validBuf.begin(), validBuf.end());

    int        out    = 0;
    auto const result = Packager::unpack(buffer, out);
    REQUIRE(result.has_value());
    CHECK(out == 77);
}

TEST_CASE("Packager: empty struct round-trip with CRC",
          "[packager]") {
    using Packager = aglio::Packager<Test::packager::Configs::Full>;

    Types::Empty           emptyIn{};
    std::vector<std::byte> buffer{};
    REQUIRE(Packager::pack(buffer, emptyIn));

    Types::Empty emptyOut{};
    auto         result = Packager::unpack(buffer, emptyOut);
    REQUIRE(result.has_value());
    CHECK(result->consumed == buffer.size());
    CHECK(emptyIn == emptyOut);
}

TEST_CASE("validate: type-dispatched unpacking via HeaderData",
          "[packager][validate]") {
    using Packager = aglio::Packager<Test::packager::PacketHeaderConfig>;

    SECTION("SensorData (typeId=1)") {
        std::vector<std::byte>             buffer{};
        Test::packager::SensorData const   dataIn{.temperature = 235, .humidity = 650};
        Test::packager::PacketHeader const hdr{.id = 100, .typeId = 1};

        REQUIRE(Packager::pack(buffer, dataIn, hdr));

        auto pkg = Packager::validate(buffer);
        REQUIRE(pkg.has_value());
        CHECK(pkg->headerData == hdr);
        CHECK(pkg->consumed == buffer.size());

        auto                              body = pkg->body;
        aglio::DynamicDeserializationView debuff{body};
        Test::packager::SensorData        dataOut{};
        REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, dataOut));
        CHECK(dataOut == dataIn);
    }

    SECTION("CommandMsg (typeId=2)") {
        std::vector<std::byte>             buffer{};
        Test::packager::CommandMsg const   msgIn{.commandCode = 0xDEAD, .payload = "reboot"};
        Test::packager::PacketHeader const hdr{.id = 200, .typeId = 2};

        REQUIRE(Packager::pack(buffer, msgIn, hdr));

        auto pkg = Packager::validate(buffer);
        REQUIRE(pkg.has_value());
        CHECK(pkg->headerData == hdr);
        CHECK(pkg->consumed == buffer.size());

        auto                              body = pkg->body;
        aglio::DynamicDeserializationView debuff{body};
        Test::packager::CommandMsg        msgOut{};
        REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, msgOut));
        CHECK(msgOut == msgIn);
    }

    SECTION("StatusReport (typeId=3)") {
        std::vector<std::byte>             buffer{};
        Test::packager::StatusReport const reportIn{.status = 1, .uptime = 86400};
        Test::packager::PacketHeader const hdr{.id = 300, .typeId = 3};

        REQUIRE(Packager::pack(buffer, reportIn, hdr));

        auto pkg = Packager::validate(buffer);
        REQUIRE(pkg.has_value());
        CHECK(pkg->headerData == hdr);
        CHECK(pkg->consumed == buffer.size());

        auto                              body = pkg->body;
        aglio::DynamicDeserializationView debuff{body};
        Test::packager::StatusReport      reportOut{};
        REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, reportOut));
        CHECK(reportOut == reportIn);
    }

    SECTION("dispatch via typeId switch") {
        std::vector<std::byte> buffer{};

        Test::packager::SensorData const   sensor{.temperature = 100, .humidity = 500};
        Test::packager::CommandMsg const   cmd{.commandCode = 42, .payload = "hello"};
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

            switch(pkg->headerData.typeId) {
            case 1:
                {
                    Test::packager::SensorData out{};
                    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, out));
                    CHECK(out == sensor);
                    CHECK(pkg->headerData.id == 1);
                }
                break;
            case 2:
                {
                    Test::packager::CommandMsg out{};
                    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, out));
                    CHECK(out == cmd);
                    CHECK(pkg->headerData.id == 2);
                }
                break;
            case 3:
                {
                    Test::packager::StatusReport out{};
                    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, out));
                    CHECK(out == status);
                    CHECK(pkg->headerData.id == 3);
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
    int const              valueIn = 1234;
    std::uint8_t const     infoIn  = 42;

    REQUIRE(Packager::pack(buffer, valueIn, infoIn));

    auto pkg = Packager::validate(buffer);
    REQUIRE(pkg.has_value());
    CHECK(pkg->headerData == infoIn);
    CHECK(pkg->consumed == buffer.size());
    CHECK(!pkg->body.empty());

    auto                              bodyCopy = pkg->body;
    int                               valueOut = 0;
    aglio::DynamicDeserializationView debuff{bodyCopy};
    REQUIRE(aglio::Serializer<std::uint32_t>::deserialize(debuff, valueOut));
    CHECK(valueOut == valueIn);
}

TEST_CASE("validate: corrupted package returns nullopt",
          "[packager][validate]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithHeaderData>;

    std::vector<std::byte> buffer{};
    int const              valueIn = 5678;
    std::uint8_t const     infoIn  = 7;

    REQUIRE(Packager::pack(buffer, valueIn, infoIn));

    buffer[11] ^= std::byte{0xFF};

    auto pkg = Packager::validate(buffer);
    CHECK(!pkg.has_value());
    CHECK(pkg.error().kind == aglio::UnpackErrorKind::ParseFailure);
}

TEST_CASE("validate: with described HeaderData",
          "[packager][validate][described]") {
    using Packager = aglio::Packager<Test::packager::Configs::WithDescribedHeaderData>;

    std::vector<std::byte>      buffer{};
    int const                   valueIn = 4321;
    Test::packager::MsgId const infoIn{.msgType = 7, .channel = 3};

    REQUIRE(Packager::pack(buffer, valueIn, infoIn));

    auto pkg = Packager::validate(buffer);
    REQUIRE(pkg.has_value());
    CHECK(pkg->headerData.msgType == infoIn.msgType);
    CHECK(pkg->headerData.channel == infoIn.channel);
    CHECK(pkg->consumed == buffer.size());
}

namespace Test::packager {

/// A vector with its bound in its type, like a static vector: capacity() is static.
template<typename T, std::size_t N>
struct CappedVector : std::vector<T> {
    using std::vector<T>::vector;

    static constexpr std::size_t capacity() { return N; }
};

struct Bounded {
    std::uint8_t                   id{};
    CappedVector<std::uint16_t, 4> values;
    std::optional<std::uint32_t>   extra;
};

/// Every config, the small and narrow ones too: always_fits is false for some of them.
using BoundConfigsList = decltype(std::tuple_cat(std::declval<ConfigsList>(),
                                                 std::declval<std::tuple<Configs::WithHeaderData,
                                                                         Configs::SmallMax,
                                                                         Configs::NarrowSize,
                                                                         Configs::TinySize>>()));

template<typename Packager,
         typename Config,
         typename Buffer,
         typename T>
bool packWithHeader(Buffer&  buffer,
                    T const& v) {
    if constexpr(requires { typename Config::HeaderData; }) {
        return Packager::pack(buffer, v, typename Config::HeaderData{});
    } else {
        return Packager::pack(buffer, v);
    }
}

}   // namespace Test::packager

TEMPLATE_LIST_TEST_CASE(
  "A value at its largest packs to max_packed_size, and only into a frame "
  "that holds it",
  "[packager][max_serialized_size]",
  Test::packager::BoundConfigsList) {
    using Packager = aglio::Packager<TestType>;
    using T        = Test::packager::Bounded;
    T const v{
      .id     = 1,
      .values = {1, 2, 3, 4},
      .extra  = 5U
    };

    static_assert(Packager::template max_packed_size<T>
                  == Packager::FrameOverhead
                       + aglio::max_serialized_size_v<T, typename TestType::Size_t>);

    std::vector<std::byte> buffer{};
    bool const             packed = Test::packager::packWithHeader<Packager, TestType>(buffer, v);

    if constexpr(Packager::template always_fits<T>) {
        static_assert(Packager::template requireAlwaysFits<T>());
        REQUIRE(packed);
        CHECK(buffer.size() == Packager::template max_packed_size<T>);

        // A buffer of exactly that size never refuses a T.
        Test::packager::FixedBuffer<Packager::template max_packed_size<T>> exact{};
        REQUIRE(Test::packager::packWithHeader<Packager, TestType>(exact, v));
        CHECK(exact.size() == exact.capacity());
    } else {
        CHECK(!packed);
        CHECK(buffer.empty());
    }
}

TEST_CASE("A type without a worst case never always fits",
          "[packager][max_serialized_size]") {
    using Packager = aglio::Packager<Test::packager::Configs::NarrowSize>;
    static_assert(!Packager::always_fits<std::vector<std::uint8_t>>);
    static_assert(Packager::always_fits<std::uint32_t>);
    static_assert(Packager::requireAlwaysFits<std::uint32_t>());
    static_assert(!Packager::always_fits<Test::packager::CappedVector<std::uint8_t, 1000>>,
                  "past NarrowSize's 512-byte MaxSize");
    static_assert(Packager::MaxFrameSize == 512);
}
