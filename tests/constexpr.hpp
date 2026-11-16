#pragma once

#include "packager_configs.hpp"
#include "types.hpp"

#include <aglio/packager.hpp>
#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

// The serializer in constant evaluation: static_assert round trips, and the bytes built at compile time
// against the bytes built at run time - the two take different paths through the leaves and the copies.
namespace Test::Constexpr {

using S = std::uint16_t;

/// `in`, serialized and read back into `out`, in one evaluation.
template<typename T>
constexpr bool roundTripsFrom(T const& in,
                              T        out) {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    if(!aglio::TypeSerializer<T, S>::serialize(in, ser)) { return false; }
    aglio::DynamicDeserializationView de{buffer};
    if(!aglio::TypeSerializer<T, S>::deserialize(out, de)) { return false; }
    return out == in && de.available() == 0;
}

/// The same from a value-initialised `out`. Not for an enum without a zero enumerator: that state is no
/// value of it.
template<typename T>
constexpr bool roundTrips(T const& in) {
    return roundTripsFrom(in, T{});
}

/// The same for a float: by its bits, since a NaN equals nothing.
template<typename T>
constexpr bool roundTripsBits(T in) {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    if(!aglio::TypeSerializer<T, S>::serialize(in, ser)) { return false; }
    T                                 out{};
    aglio::DynamicDeserializationView de{buffer};
    if(!aglio::TypeSerializer<T, S>::deserialize(out, de)) { return false; }
    return std::bit_cast<std::array<std::byte, sizeof(T)>>(out)
        == std::bit_cast<std::array<std::byte, sizeof(T)>>(in);
}

/// What `in` serializes to: the count of bytes and the bytes, the rest of the array zero.
struct Wire {
    std::size_t               size{};
    std::array<std::byte, 96> bytes{};

    constexpr bool operator==(Wire const&) const = default;
};

template<typename T>
constexpr Wire wireOf(T const& in) {
    Types::StaticVec<std::byte, 96> buffer;
    aglio::DynamicSerializationView ser{buffer};
    Wire                            wire{};
    if(!aglio::TypeSerializer<T, S>::serialize(in, ser)) { return wire; }
    wire.size  = buffer.size();
    wire.bytes = buffer.storage;
    return wire;
}

/// A wire of these bytes.
template<typename... Bs>
constexpr Wire wire(Bs... bs) {
    Wire w{};
    w.size = sizeof...(Bs);
    std::size_t i{};
    ((w.bytes[i++] = std::byte{static_cast<unsigned char>(bs)}), ...);
    return w;
}

struct Fixed {
    std::uint8_t                 id{};
    std::int32_t                 offset{};
    float                        gain{};
    Types::Color                 color{Types::Color::red};
    bool                         on{};
    std::array<std::uint16_t, 2> raw{};
    std::chrono::milliseconds    period{};

    constexpr bool operator==(Fixed const&) const = default;
};

struct Growing {
    std::uint16_t                                               id{};
    Types::StaticVec<std::uint16_t, 4>                          samples{};
    std::optional<std::int8_t>                                  trim{};
    std::variant<std::uint8_t, std::pair<std::uint8_t, double>> either{};
    Types::StaticVec<bool, 3>                                   flags{};
    std::expected<std::uint32_t, std::uint8_t>                  result{};

    constexpr bool operator==(Growing const&) const = default;
};

inline constexpr Fixed fixedSample{
  .id     = 7,
  .offset = -2,
  .gain   = 1.5F,
  .color  = Types::Color::blue,
  .on     = true,
  .raw    = {0x0102, 0xFFFE},
  .period = std::chrono::milliseconds{-3}
};

inline constexpr Growing growingSample{
  .id      = 0xBEEF,
  .samples = {1, 2, 0x8000},
  .trim    = std::int8_t{-1},
  .either  = std::pair<std::uint8_t, double>{9, -0.5},
  .flags   = {true, false},
  .result  = std::unexpected{std::uint8_t{4}}
};

// every kind of leaf
static_assert(roundTrips(std::int8_t{-5}));
static_assert(roundTrips(std::uint16_t{0xBEEF}));
static_assert(roundTrips(std::int32_t{-123456}));
static_assert(roundTrips(std::uint64_t{0x0123'4567'89AB'CDEFULL}));
static_assert(roundTrips(char{'x'}));
static_assert(roundTrips(std::byte{0xA5}));
static_assert(roundTrips(true));
static_assert(roundTrips(false));
static_assert(roundTripsFrom(Types::Color::green,
                             Types::Color::red));
static_assert(roundTrips(Types::inactive));
static_assert(roundTripsFrom(static_cast<Types::Color>(200),
                             Types::Color::red));
static_assert(roundTripsBits(1.5F));
static_assert(roundTripsBits(-0.0));
static_assert(roundTripsBits(std::numeric_limits<float>::quiet_NaN()));
static_assert(roundTripsBits(std::numeric_limits<double>::denorm_min()));

// wrappers, tuples, durations
static_assert(roundTrips(std::optional<std::uint32_t>{42U}));
static_assert(roundTrips(std::optional<std::uint32_t>{}));
static_assert(roundTrips(std::expected<std::int16_t,
                                       std::uint8_t>{std::int16_t{-7}}));
static_assert(roundTrips(std::expected<std::int16_t,
                                       std::uint8_t>{std::unexpected{std::uint8_t{3}}}));
static_assert(roundTrips(std::variant<std::uint8_t,
                                      float,
                                      std::int64_t>{std::int64_t{-9}}));
static_assert(roundTrips(std::variant<std::uint8_t,
                                      float,
                                      std::int64_t>{std::uint8_t{1}}));
static_assert(roundTrips(std::pair<std::uint8_t,
                                   std::uint32_t>{1,
                                                  2}));
static_assert(roundTrips(std::tuple<std::int8_t,
                                    double,
                                    bool>{-1,
                                          2.5,
                                          true}));
static_assert(roundTrips(std::chrono::microseconds{-12345}));
static_assert(roundTrips(std::chrono::hours{-48}));
static_assert(roundTrips(std::chrono::duration<std::int64_t,
                                               std::ratio<60>>{90}));

// ranges: numbers at once, bools and strings and nested ones element by element
static_assert(roundTrips(std::array<std::uint16_t,
                                    3>{1,
                                       0x8000,
                                       0xFFFF}));
static_assert(roundTrips(std::array<int,
                                    0>{}));
static_assert(roundTrips(std::array<bool,
                                    3>{true,
                                       false,
                                       true}));
static_assert(roundTripsFrom(std::array<Types::Color,
                                        2>{Types::Color::red,
                                           Types::Color::blue},
                             std::array<Types::Color,
                                        2>{Types::Color::green,
                                           Types::Color::green}));
static_assert(roundTrips(Types::StaticVec<std::uint32_t,
                                          4>{7U,
                                             8U,
                                             9U}));
static_assert(roundTrips(Types::StaticVec<std::byte,
                                          4>{std::byte{0},
                                             std::byte{0xFF}}));
static_assert(roundTrips(Types::StaticVec<bool,
                                          4>{false,
                                             true}));
static_assert(roundTrips(std::vector<std::int64_t>{-1,
                                                   0,
                                                   1}));
static_assert(roundTrips(std::string{"a string longer than the small buffer of one"}));
static_assert(roundTrips(std::vector<std::optional<std::uint8_t>>{std::uint8_t{1},
                                                                  std::nullopt}));
static_assert(roundTrips(std::vector<std::string>{"one",
                                                  "two"}));

// described structs: of fixed size (one claim of the buffer) and one that grows
static_assert(aglio::HasFixedSerializedSize<Fixed,
                                            S>);
static_assert(!aglio::HasFixedSerializedSize<Growing,
                                             S>);
static_assert(roundTrips(fixedSample));
static_assert(roundTrips(growingSample));
static_assert(roundTrips(Types::Empty{}));
static_assert(roundTrips(std::vector<Fixed>{fixedSample,
                                            Fixed{}}));

// the bytes themselves: little endian, a count in front of a range, a flag or an index in front of a wrapper
static_assert(wireOf(std::uint32_t{0x0403'0201})
              == wire(1,
                      2,
                      3,
                      4));
static_assert(wireOf(std::int16_t{-2})
              == wire(0xFE,
                      0xFF));
static_assert(wireOf(1.0F)
              == wire(0x00,
                      0x00,
                      0x80,
                      0x3F));
static_assert(wireOf(Types::Color::blue) == wire(3));
static_assert(wireOf(true) == wire(1));
// a duration of a minute or longer: 32 bits, whatever counts it in memory
static_assert(wireOf(std::chrono::hours{-2})
              == wire(0xFE,
                      0xFF,
                      0xFF,
                      0xFF));
static_assert(wireOf(std::chrono::duration<std::int64_t,
                                           std::ratio<3600>>{-2})
              == wire(0xFE,
                      0xFF,
                      0xFF,
                      0xFF));
static_assert(wireOf(std::chrono::seconds{-2}).size == 8);
static_assert(wireOf(std::chrono::duration<std::int64_t,
                                           std::ratio<3600>>{std::int64_t{1} << 40})
                  .size
                == 0,
              "a count 32 bits cannot hold is refused");
static_assert(wireOf(std::optional<std::uint8_t>{std::uint8_t{9}})
              == wire(1,
                      9));
static_assert(wireOf(std::optional<std::uint8_t>{}) == wire(0));
static_assert(wireOf(std::variant<std::uint8_t,
                                  std::uint16_t>{std::uint16_t{0x0201}})
              == wire(1,
                      1,
                      2));
static_assert(wireOf(std::array<std::uint16_t,
                                2>{0x0102,
                                   0x0304})
              == wire(2,
                      0,
                      2,
                      1,
                      4,
                      3));
static_assert(wireOf(std::array<bool,
                                2>{true,
                                   false})
              == wire(2,
                      0,
                      1,
                      0));
static_assert(wireOf(Types::StaticVec<std::uint8_t,
                                      4>{5,
                                         6})
              == wire(2,
                      0,
                      5,
                      6));
static_assert(wireOf(fixedSample).size
              == aglio::serializedSizeV<Fixed,
                                        S>);

// what does not fit, and what is cut short, is refused at compile time as at run time
constexpr bool refusesShortBuffer() {
    std::array<std::byte, 3>        small{};
    aglio::DynamicSerializationView ser{small};
    return !aglio::TypeSerializer<std::uint32_t, S>::serialize(1U, ser)
        && !aglio::TypeSerializer<Fixed, S>::serialize(fixedSample, ser);
}

static_assert(refusesShortBuffer());

constexpr bool refusesTruncated() {
    std::vector<std::byte>          buffer;
    aglio::DynamicSerializationView ser{buffer};
    if(!aglio::TypeSerializer<Growing, S>::serialize(growingSample, ser)) { return false; }
    for(std::size_t keep = 0; keep != buffer.size(); ++keep) {
        std::vector<std::byte> const cut(
          buffer.begin(),
          std::next(buffer.begin(), static_cast<std::ptrdiff_t>(keep)));
        Growing                           out{};
        aglio::DynamicDeserializationView de{cut};
        if(aglio::TypeSerializer<Growing, S>::deserialize(out, de)) { return false; }
    }
    return true;
}

static_assert(refusesTruncated());

// a bool, alone or in a range, is true or false whatever byte it is read from
constexpr bool boolFromAnyByte() {
    constexpr std::array<std::byte, 5> bytes{std::byte{3},
                                             std::byte{0},
                                             std::byte{2},
                                             std::byte{0xFF},
                                             std::byte{7}};
    aglio::DynamicDeserializationView  de{bytes};
    std::array<bool, 3>                flags{};
    bool                               last{};
    return aglio::TypeSerializer<std::array<bool, 3>, std::uint8_t>::deserialize(flags, de)
        && aglio::TypeSerializer<bool, std::uint8_t>::deserialize(last, de)
        && flags == std::array<bool, 3>{false, true, true} && last;
}

static_assert(boolFromAnyByte());

// A floating point type whose bytes are not all its value has no wire form.
static_assert(aglio::Detail::floatFillsItsBytes<float>());
static_assert(aglio::Detail::floatFillsItsBytes<double>());
static_assert(aglio::Detail::floatFillsItsBytes<long double>()
              == (std::numeric_limits<long double>::digits == 53
                  || std::numeric_limits<long double>::digits == 113));

// --- the Packager ---------------------------------------------------------------------------------

/// A frame: the count of bytes and the bytes, the rest zero.
using Frame = Wire;

/// Packs with the header data a config asks for, if it asks for one.
template<typename Config,
         typename Buffer,
         typename T>
constexpr bool packWith(Buffer&  buffer,
                        T const& value) {
    using P = aglio::Packager<Config>;
    if constexpr(requires { typename Config::HeaderData; }) {
        typename Config::HeaderData info{};
        if constexpr(requires { info.msgType; }) {
            info.msgType = 5;
            info.channel = 9;
        } else {
            info = 0x5A;
        }
        return P::pack(buffer, value, info);
    } else {
        return P::pack(buffer, value);
    }
}

/// The header data packWith puts in came back, where there is one.
template<typename Config>
constexpr bool headerDataBack(auto const& result) {
    if constexpr(requires { result.headerData.msgType; }) {
        return result.headerData.msgType == 5 && result.headerData.channel == 9;
    } else if constexpr(requires { typename Config::HeaderData; }) {
        return result.headerData == 0x5A;
    } else {
        return true;
    }
}

/// Packed into a std::vector and unpacked again, in one evaluation; behind three bytes of noise too.
template<typename Config,
         typename T>
constexpr bool packsAndUnpacks(T const& in) {
    using P = aglio::Packager<Config>;
    std::vector<std::byte> frame;
    if(!packWith<Config>(frame, in)) { return false; }
    T          out{};
    auto const result = P::unpack(frame, out);
    if(!result || result->consumed != frame.size() || !(out == in)) { return false; }
    if(!headerDataBack<Config>(*result)) { return false; }

    std::vector<std::byte> noisy{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    if(!packWith<Config>(noisy, in)) { return false; }
    T          again{};
    auto const skipped = P::unpack(noisy, again);
    if constexpr(requires { Config::packageStart; }) {
        // the start marker is how the noise in front is told from the frame
        return skipped && skipped->consumed == noisy.size() && again == in;
    } else {
        return true;
    }
}

/// A fixed-capacity buffer with std::string's resize_and_overwrite: packed in place, unfilled.
template<std::size_t N>
struct RawStaticVec : Types::StaticVec<std::byte, N> {
    template<typename Op>
    constexpr void resize_and_overwrite(std::size_t n,
                                        Op&&        op) {
        this->count = op(this->storage.data(), n);
    }
};

static_assert(aglio::Detail::SizedOnce<Types::StaticVec<std::byte,
                                                        96>>);
static_assert(!aglio::Detail::Overwritable<Types::StaticVec<std::byte,
                                                            96>>);
static_assert(aglio::Detail::Overwritable<RawStaticVec<96>>);

/// The frame a value packs to, through a buffer of the kind `Buffer`.
template<typename Config,
         typename Buffer,
         typename T>
constexpr Frame frameOf(T const& in) {
    Buffer buffer{};
    Frame  frame{};
    if(!packWith<Config>(buffer, in)) { return frame; }
    frame.size = buffer.size();
    std::ranges::copy(buffer, frame.bytes.begin());
    return frame;
}

/// A cut-off frame asks for more data, a damaged one is not taken for a frame.
template<typename Config,
         typename T>
constexpr bool refusesBadFrames(T const& in) {
    using P = aglio::Packager<Config>;
    std::vector<std::byte> frame;
    if(!packWith<Config>(frame, in)) { return false; }

    std::vector<std::byte> cut(frame.begin(), std::prev(frame.end()));
    T                      out{};
    auto const             more = P::unpack(cut, out);
    if(more || more.error().kind != aglio::UnpackErrorKind::needMoreData) { return false; }

    std::vector<std::byte> damaged{frame};
    damaged.back() ^= std::byte{0x01};   // the last byte: the body's CRC
    T          other{};
    auto const bad = P::unpack(damaged, other);
    return !bad;
}

namespace C = ::Test::packager::Configs;

#define AGLIO_CONSTEXPR_PACKS(Config)                              \
    static_assert(packsAndUnpacks<Config>(fixedSample));           \
    static_assert(packsAndUnpacks<Config>(growingSample));         \
    static_assert(packsAndUnpacks<Config>(Types::Empty{}));        \
    static_assert(packsAndUnpacks<Config>(std::string{"a frame"}))

AGLIO_CONSTEXPR_PACKS(C::Minimal);
AGLIO_CONSTEXPR_PACKS(C::SimplePackageStart);
AGLIO_CONSTEXPR_PACKS(C::SimpleCrc);
AGLIO_CONSTEXPR_PACKS(C::CrcNoHeader);
AGLIO_CONSTEXPR_PACKS(C::Full);
AGLIO_CONSTEXPR_PACKS(C::FullNoHeaderCrc);
AGLIO_CONSTEXPR_PACKS(C::WithHeaderData);
AGLIO_CONSTEXPR_PACKS(C::WithDescribedHeaderData);
AGLIO_CONSTEXPR_PACKS(C::WithDescribedCrc);
AGLIO_CONSTEXPR_PACKS(C::NarrowSize);
AGLIO_CONSTEXPR_PACKS(C::TinySize);

#undef AGLIO_CONSTEXPR_PACKS

static_assert(refusesBadFrames<C::Full>(fixedSample));
static_assert(refusesBadFrames<C::CrcNoHeader>(growingSample));
static_assert(refusesBadFrames<C::WithDescribedCrc>(fixedSample));

// a frame too long for the config, and a buffer too small for the frame: refused, the buffer as it was
constexpr bool refusesWhatDoesNotFit() {
    std::vector<std::byte> buffer{std::byte{0xEE}};
    if(aglio::Packager<C::SmallMax>::pack(buffer, fixedSample)) { return false; }
    Types::StaticVec<std::byte, 8> small{std::byte{0xEE}};
    if(aglio::Packager<C::Full>::pack(small, fixedSample)) { return false; }
    return buffer.size() == 1 && small.size() == 1 && small.storage[0] == std::byte{0xEE};
}

static_assert(refusesWhatDoesNotFit());

// the three ways into a buffer give one frame: grown per field, one window, one window unfilled
static_assert(frameOf<C::Full,
                      std::vector<std::byte>>(growingSample)
              == frameOf<C::Full,
                         Types::StaticVec<std::byte,
                                          96>>(growingSample));
static_assert(frameOf<C::Full,
                      std::vector<std::byte>>(growingSample)
              == frameOf<C::Full,
                         RawStaticVec<96>>(growingSample));
static_assert(frameOf<C::Full,
                      RawStaticVec<96>>(growingSample)
                .size
              != 0);

// the frame itself: start marker, size, header CRC, body, body CRC (MyCrc: the sum of the bytes)
static_assert(frameOf<C::SimplePackageStart,
                      RawStaticVec<96>>(std::uint16_t{0x0201})
              == wire(0xCD,
                      0xAB,
                      2,
                      0,
                      0,
                      0,
                      1,
                      2));
static_assert(frameOf<C::Full,
                      RawStaticVec<96>>(std::uint8_t{7})
              == wire(0xCD,
                      0xAB,
                      5,
                      0,
                      0,
                      0,
                      0x7D,
                      1,
                      0,
                      0,
                      7,
                      7,
                      0,
                      0,
                      0));

// validate: the body handed out without unpacking it
constexpr bool validates() {
    using P = aglio::Packager<C::WithHeaderData>;
    std::vector<std::byte> frame;
    if(!P::pack(frame, std::uint16_t{0x0201}, std::uint8_t{0x5A})) { return false; }
    auto const result = P::validate(frame);
    return result && result->consumed == frame.size() && result->headerData == 0x5A
        && result->body.size() == 2 && result->body[0] == std::byte{1}
        && result->body[1] == std::byte{2};
}

static_assert(validates());

}   // namespace Test::Constexpr

// The two paths give the same bytes: the constant-evaluated ones compared with those of a call at run time.
TEST_CASE("Serializer: the bytes built at compile time are the bytes built at run time",
          "[serializer][constexpr]") {
    using namespace Test::Constexpr;

    auto const same = [](auto const& value, Wire const& atCompileTime) {
        // not const: a const object's initializer may be evaluated as a constant, taking the other path
        Wire atRunTime = wireOf(value);
        return atRunTime == atCompileTime && atRunTime.size != 0;
    };

#define AGLIO_SAME_BYTES(...)                                  \
    do {                                                       \
        static constexpr ::Test::Constexpr::Wire atCompileTime \
          = ::Test::Constexpr::wireOf(__VA_ARGS__);            \
        CHECK(same(__VA_ARGS__, atCompileTime));               \
    } while(false)

    AGLIO_SAME_BYTES(std::uint8_t{0xA5});
    AGLIO_SAME_BYTES(std::int32_t{-123456});
    AGLIO_SAME_BYTES(std::uint64_t{0x0123'4567'89AB'CDEFULL});
    AGLIO_SAME_BYTES(2.5F);
    AGLIO_SAME_BYTES(-1.0e-300);
    AGLIO_SAME_BYTES(Types::Color::green);
    AGLIO_SAME_BYTES(Types::active);
    AGLIO_SAME_BYTES(true);
    AGLIO_SAME_BYTES(std::optional<std::int16_t>{std::int16_t{-2}});
    AGLIO_SAME_BYTES(std::variant<std::uint8_t, double>{0.25});
    AGLIO_SAME_BYTES(std::tuple<std::int8_t, std::uint32_t>{-1, 0xDEADBEEFU});
    AGLIO_SAME_BYTES(std::chrono::nanoseconds{9876543210LL});
    AGLIO_SAME_BYTES(std::array<std::uint32_t, 3>{1U, 0x8000'0000U, 0xFFFF'FFFFU});
    AGLIO_SAME_BYTES(std::array<bool, 4>{true, false, true, true});
    AGLIO_SAME_BYTES(Types::StaticVec<std::int16_t, 4>{std::int16_t{-1}, std::int16_t{2}});
    AGLIO_SAME_BYTES(fixedSample);
    AGLIO_SAME_BYTES(growingSample);

#undef AGLIO_SAME_BYTES

    // and what was written at compile time reads back at run time
    static constexpr Wire             wire = wireOf(growingSample);
    Growing                           out{};
    aglio::DynamicDeserializationView de{wire.bytes};
    REQUIRE(aglio::TypeSerializer<Growing, S>::deserialize(out, de));
    CHECK(out == growingSample);
    CHECK(de.available() == wire.bytes.size() - wire.size);
}

TEST_CASE("Packager: the frame built at compile time is the frame built at run time",
          "[packager][constexpr]") {
    using namespace Test::Constexpr;

    auto const check = []<typename Config>(Frame const& atCompileTime) {
        // not const: see the serializer's case
        Frame viaVector = frameOf<Config, std::vector<std::byte>>(growingSample);
        Frame viaWindow = frameOf<Config, RawStaticVec<96>>(growingSample);
        // bools: Catch2 would print the frames, and std::byte has no printer
        bool const sameAsVector = viaVector == atCompileTime;
        bool const sameAsWindow = viaWindow == atCompileTime;
        CHECK(atCompileTime.size != 0);
        CHECK(sameAsVector);
        CHECK(sameAsWindow);

        // and a frame from compile time unpacks at run time
        Growing    out{};
        auto       frame  = std::span{atCompileTime.bytes}.first(atCompileTime.size);
        auto const result = aglio::Packager<Config>::unpack(frame, out);
        REQUIRE(result.has_value());
        CHECK(result->consumed == atCompileTime.size);
        CHECK(out == growingSample);
    };

#define AGLIO_SAME_FRAME(Config)                                                                 \
    do {                                                                                         \
        static constexpr Frame atCompileTime = frameOf<Config, RawStaticVec<96>>(growingSample); \
        check.template         operator()<Config>(atCompileTime);                                \
    } while(false)

    AGLIO_SAME_FRAME(C::Minimal);
    AGLIO_SAME_FRAME(C::SimplePackageStart);
    AGLIO_SAME_FRAME(C::CrcNoHeader);
    AGLIO_SAME_FRAME(C::Full);
    AGLIO_SAME_FRAME(C::WithDescribedHeaderData);
    AGLIO_SAME_FRAME(C::WithDescribedCrc);
    AGLIO_SAME_FRAME(C::TinySize);

#undef AGLIO_SAME_FRAME
}
