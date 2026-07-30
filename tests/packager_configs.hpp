#pragma once

#include <aglio/packager.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

// Packager configurations shared by the Catch2 tests, the fuzz target and the fuzz corpus
// generator. Kept free of Catch2 so the fuzz binaries do not need to link it.
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

    // The wraparound is intentional for a checksum, so it is done in unsigned arithmetic with an
    // explicit narrowing cast - `+=` on a std::uint8_t promotes to int and trips UBSan's
    // implicit-conversion check, which is fatal in the fuzz build.
    static type calc(std::span<std::byte const> data) {
        type r{};
        for(auto b : data) {
            r.msg_type = static_cast<std::uint8_t>((r.msg_type + static_cast<unsigned>(b)) & 0xFFu);
            r.channel
              = static_cast<std::uint8_t>((r.channel + static_cast<unsigned>(b) * 3u) & 0xFFu);
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

    // Body size limit far below what the message types serialize to, so the MaxSize rejection
    // path is reachable.
    struct SmallMax {
        using Size_t                                = std::uint32_t;
        static constexpr std::uint32_t MaxSize      = 4;
        static constexpr std::uint16_t PackageStart = 0xABCD;
    };

    // Narrow size field: a 16 bit Size_t makes the header layout differ from the uint32_t
    // configs above and keeps MaxSize small enough to be hit by fuzzed bodies.
    struct NarrowSize {
        using Crc                                   = MyCrc;
        using Size_t                                = std::uint16_t;
        static constexpr Size_t        MaxSize      = 512;
        static constexpr std::uint16_t PackageStart = 0x55AA;
    };

    // One byte size field, for both the framing and the length prefixes the serializer writes.
    // A range with more than 255 elements cannot be expressed, which is how the serializer's
    // "size exceeds Size_t" guard becomes reachable from small inputs.
    struct TinySize {
        using Crc                                  = MyCrc;
        using Size_t                               = std::uint8_t;
        static constexpr Size_t       MaxSize      = 200;
        static constexpr std::uint8_t PackageStart = 0x7E;
    };

}   // namespace Configs

}   // namespace Test::packager
