#pragma once

#include "serialization_buffers.hpp"
#include "serializer.hpp"

#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>

namespace aglio {

enum class UnpackErrorKind : std::uint8_t {
    NeedMoreData,
    ParseFailure,
};

namespace detail {

    template<typename T>
    struct HeaderDataField {
        T headerData{};
    };

    struct NoHeaderDataField {};

    /// Instantiated by Packager::requireAlwaysFits: a failure names both sizes as template
    /// arguments, FrameHolds<max_packed_size, MaxFrameSize>.
    template<std::size_t MaxPackedSize, std::size_t MaxFrameSize>
    struct FrameHolds {
        static_assert(MaxPackedSize <= MaxFrameSize,
                      "a value of this type at its largest packs into a frame longer than the "
                      "Packager's MaxSize (template arguments: max_packed_size, MaxFrameSize)");
        static constexpr bool value = true;
    };

    template<typename T>
    struct HasNoWorstCase {
        static_assert(false,
                      "this type has no worst-case wire size (aglio::has_max_serialized_size is "
                      "false): it holds a container without a static capacity, or a custom "
                      "serializer without a max_serialized_size member");
        static constexpr bool value = false;
    };

    template<typename T>
    constexpr bool is_trivial_v
      = std::is_trivially_default_constructible_v<T> && std::is_trivially_copyable_v<T>;

    /// A buffer whose max_size() equals its capacity, known at compile time (a static vector or
    /// an adapter around one). A std::vector never qualifies.
    template<typename B>
    constexpr bool fixed_capacity_v = [] {
        using U = std::remove_cvref_t<B>;
        if constexpr(requires { U::FixedCapacity; }) {
            return bool{U::FixedCapacity};
        } else if constexpr(requires {
                                typename std::bool_constant<(U::max_size() == U::capacity())>;
                            })
        {
            return U::max_size() == U::capacity();
        } else {
            return false;
        }
    }();

    /// A buffer the serializer writes as one window from data(), setting its size afterwards.
    template<typename B>
    concept SizedOnce = fixed_capacity_v<B> && requires(B& b) {
        { b.data() } -> std::convertible_to<std::byte*>;
        { b.size() } -> std::convertible_to<std::size_t>;
        b.resize(std::size_t{});
        b.max_size();
    };

    /// The free room of a fixed-capacity buffer.
    struct Window {
        std::byte*  first;
        std::size_t count;

        [[nodiscard]] constexpr std::byte* data() const noexcept { return first; }

        [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
    };

    /// resize_and_overwrite op that keeps every byte.
    struct KeepAll {
        constexpr std::size_t operator()(auto*,
                                         std::size_t n) const noexcept {
            return n;
        }
    };

    /// A fixed-capacity buffer with std::string's resize_and_overwrite(n, op), so growing it
    /// fills nothing. Without it the buffer is resized to max_size() and trimmed after.
    template<typename B>
    concept Overwritable
      = fixed_capacity_v<B> && requires(B& b) { b.resize_and_overwrite(std::size_t{}, KeepAll{}); };

    template<typename Serializer, typename UserConfig>
    struct Packager {
    private:
        struct Config : UserConfig {
            struct NoCrc {
                using type = std::uint8_t;
            };

            static constexpr bool UseCrc       = requires { typename UserConfig::Crc; };
            static constexpr bool UseHeaderCrc = [] {
                if constexpr(requires { UserConfig::UseHeaderCrc; }) {
                    return UserConfig::UseHeaderCrc && UseCrc;
                } else {
                    return UseCrc;
                }
            }();
            static constexpr bool UsePackageStart = requires { UserConfig::PackageStart; };
            using Crc                             = decltype([] {
                if constexpr(UseCrc) {
                    return typename UserConfig::Crc{};
                } else {
                    return NoCrc{};
                }
            }());
            static constexpr auto PackageStart    = [] {
                if constexpr(requires { UserConfig::PackageStart; }) {
                    return UserConfig::PackageStart;
                } else {
                    return std::uint8_t{};
                }
            }();
            using Size_t                  = typename UserConfig::Size_t;
            static constexpr auto MaxSize = [] {
                if constexpr(requires { UserConfig::MaxSize; }) {
                    return UserConfig::MaxSize;
                } else {
                    return std::numeric_limits<Size_t>::max();
                }
            }();
            static constexpr bool UseHeaderData = requires { typename UserConfig::HeaderData; };
            using HeaderData                    = decltype([] {
                if constexpr(UseHeaderData) {
                    return typename UserConfig::HeaderData{};
                } else {
                    return std::uint8_t{};
                }
            }());
        };

        static_assert(!Config::UseHeaderData || Config::UseHeaderCrc,
                      "HeaderData requires UseHeaderCrc to be enabled");

        using PackageStart_t = std::remove_cvref_t<decltype(Config::PackageStart)>;
        using Crc_t          = std::remove_cvref_t<typename Config::Crc::type>;
        using Size_t         = std::remove_cvref_t<typename Config::Size_t>;
        using HeaderData_t   = std::remove_cvref_t<typename Config::HeaderData>;

        static constexpr PackageStart_t PackageStart{Config::PackageStart};
        static constexpr std::byte      FirstByte{PackageStart & 0xFF};
        static constexpr Size_t         MaxSize{Config::MaxSize};

        static_assert(is_trivial_v<PackageStart_t> || !Config::UsePackageStart,
                      "PackageStart must be trivial");
        static_assert(!Config::UseCrc
                        || aglio::HasFixedSerializedSize<Crc_t,
                                                         Size_t>,
                      "Crc::type must have a fixed serialized size");
        static_assert(!Config::UseCrc || std::equality_comparable<Crc_t>,
                      "Crc::type must be equality comparable");
        static_assert(is_trivial_v<Size_t>,
                      "Size_t must be trivial");
        static_assert(!Config::UseHeaderData
                        || aglio::HasFixedSerializedSize<HeaderData_t,
                                                         Size_t>,
                      "HeaderData must have a fixed serialized size");
        static_assert(std::numeric_limits<Size_t>::max() >= MaxSize,
                      "max size needs to fit into Size_t");
        static_assert(std::endian::native == std::endian::little,
                      "needs little endian");

        static constexpr std::size_t PackageStartSize{
          Config::UsePackageStart ? sizeof(PackageStart_t) : 0};

        static constexpr std::size_t PackageSizeSize{sizeof(Size_t)};

        static constexpr std::size_t CrcSize{
          Config::UseCrc ? aglio::serialized_size_v<Crc_t, Size_t> : 0};

        static constexpr std::size_t HeaderDataSize{
          Config::UseHeaderData ? aglio::serialized_size_v<HeaderData_t, Size_t> : 0};

        static constexpr std::size_t HeaderSize{PackageStartSize + PackageSizeSize + HeaderDataSize
                                                + (Config::UseHeaderCrc ? CrcSize : 0)};

        template<typename Buffer>
        struct BufferAdapter {
        private:
            Buffer&           buffer;
            std::size_t const startSize;
            std::size_t       finalizedEnd{0};
            bool              finalized{false};

        public:
            static constexpr bool FixedCapacity = fixed_capacity_v<Buffer>;

            explicit BufferAdapter(Buffer& buffer_ AGLIO_LIFETIMEBOUND)
              : buffer{buffer_}
              , startSize{buffer.size()} {}

            BufferAdapter(BufferAdapter const&)            = delete;
            BufferAdapter(BufferAdapter&&)                 = delete;
            BufferAdapter& operator=(BufferAdapter const&) = delete;
            BufferAdapter& operator=(BufferAdapter&&)      = delete;

            [[nodiscard]] std::size_t size() const { return buffer.size() - startSize; }

            [[nodiscard]] std::size_t finalizedSize() const { return finalizedEnd - startSize; }

            void resize(std::size_t newSize) { buffer.resize(newSize + startSize); }

            template<typename Op>
                requires Overwritable<Buffer>
            void resize_and_overwrite(std::size_t newSize,
                                      Op&&        op) {
                buffer.resize_and_overwrite(newSize + startSize, [&](auto* first, std::size_t n) {
                    return op(std::next(first,
                                        static_cast<std::make_signed_t<std::size_t>>(startSize)),
                              n - startSize)
                         + startSize;
                });
            }

            /// Grows for bytes the caller will overwrite; unfilled where the buffer allows.
            void resizeForOverwrite(std::size_t newSize) {
                if constexpr(Overwritable<Buffer>) {
                    resize_and_overwrite(newSize, KeepAll{});
                } else {
                    resize(newSize);
                }
            }

            [[nodiscard]] std::size_t max_size() const {
                if constexpr(requires { buffer.max_size(); }) {
                    auto const bufferMaxSize = static_cast<std::size_t>(buffer.max_size());
                    auto const adjustedMax
                      = bufferMaxSize > startSize ? bufferMaxSize - startSize : std::size_t{0};
                    return std::min(adjustedMax, static_cast<std::size_t>(MaxSize));
                } else {
                    return static_cast<std::size_t>(MaxSize);
                }
            }

            auto& operator[](std::size_t pos) { return buffer[pos]; }

            void finalize() {
                finalizedEnd = buffer.size();
                finalized    = true;
            }

            auto data() {
                return std::next(buffer.data(),
                                 static_cast<std::make_signed_t<std::size_t>>(startSize));
            }

            auto asSpan() { return std::span{std::ranges::subrange(begin(), end())}; }

            auto begin() {
                return std::next(buffer.begin(),
                                 static_cast<std::make_signed_t<std::size_t>>(startSize));
            }

            auto end() {
                if(finalized) {
                    return std::next(buffer.begin(),
                                     static_cast<std::make_signed_t<std::size_t>>(finalizedEnd));
                }
                return std::next(begin(), static_cast<std::make_signed_t<std::size_t>>(size()));
            }

            bool empty() {
                if(finalized) { return finalizedEnd - startSize == 0; }
                return buffer.size() <= startSize;
            }
        };

        template<typename T,
                 std::size_t N>
        static bool headerWrite(std::span<std::byte> buf,
                                T const&             v) {
            auto                            sub = buf.first<N>();
            aglio::DynamicSerializationView sebuf{sub};
            return aglio::serializer<T, Size_t>::serialize(v, sebuf);
        }

        template<typename T,
                 std::size_t N>
        static bool headerRead(T&                         v,
                               std::span<std::byte const> buf) {
            auto                              sub = buf.first<N>();
            aglio::DynamicDeserializationView debuf{sub};
            return aglio::serializer<T, Size_t>::deserialize(v, debuf);
        }

        /// A failed pack leaves the buffer unchanged.
        template<typename T,
                 typename Buffer>
        static constexpr bool packImpl(Buffer&             buffer,
                                       T const&            v,
                                       HeaderData_t const& info) {
            auto const startSize = buffer.size();
            if(packInto(buffer, v, info)) { return true; }
            buffer.resize(startSize);
            return false;
        }

        template<typename T,
                 typename Buffer>
        static constexpr bool packInto(Buffer&             buffer,
                                       T const&            v,
                                       HeaderData_t const& info) {
            BufferAdapter<Buffer> headerBuffer{buffer};
            if(headerBuffer.max_size() < HeaderSize) { return false; }
            headerBuffer.resizeForOverwrite(HeaderSize);

            BufferAdapter<decltype(headerBuffer)> bodyBuffer{headerBuffer};
            if(!Serializer::serialize(bodyBuffer, v)) { return false; }
            bodyBuffer.finalize();

            if constexpr(Config::UseCrc && Config::UseHeaderCrc) {
                BufferAdapter<decltype(bodyBuffer)> crcBuffer{bodyBuffer};
                auto const                          bodyCrc = Config::Crc::calc(std::as_bytes(
                  std::span(std::ranges::subrange(bodyBuffer.begin(), bodyBuffer.end()))));

                if(crcBuffer.max_size() < CrcSize) { return false; }
                crcBuffer.resizeForOverwrite(CrcSize);
                if(!headerWrite<Crc_t, CrcSize>(crcBuffer.asSpan(), bodyCrc)) { return false; }
                crcBuffer.finalize();
            }

            Size_t const bodySize
              = static_cast<Config::Size_t>(bodyBuffer.finalizedSize() + CrcSize);

            if constexpr(Config::UsePackageStart) {
                copyFixed<PackageStartSize>(headerBuffer.data(), std::addressof(PackageStart));
            }

            copyFixed<PackageSizeSize>(std::next(headerBuffer.data(), PackageStartSize),
                                       std::addressof(bodySize));

            if constexpr(Config::UseCrc && !Config::UseHeaderCrc) {
                BufferAdapter<decltype(bodyBuffer)> crcBuffer{bodyBuffer};
                auto const                          bodyCrc = Config::Crc::calc(std::as_bytes(
                  std::span(std::ranges::subrange(headerBuffer.begin(), bodyBuffer.end()))));

                if(crcBuffer.max_size() < CrcSize) { return false; }
                crcBuffer.resizeForOverwrite(CrcSize);
                if(!headerWrite<Crc_t, CrcSize>(crcBuffer.asSpan(), bodyCrc)) { return false; }
                crcBuffer.finalize();
            }

            if constexpr(Config::UseHeaderCrc) {
                if constexpr(Config::UseHeaderData) {
                    if(!headerWrite<HeaderData_t, HeaderDataSize>(
                         headerBuffer.asSpan().subspan(PackageStartSize + PackageSizeSize),
                         info))
                    {
                        return false;
                    }
                }

                auto const headerCrc
                  = Config::Crc::calc(std::as_bytes(std::span(std::ranges::subrange(
                    headerBuffer.begin(),
                    std::next(headerBuffer.begin(),
                              PackageStartSize + PackageSizeSize + HeaderDataSize)))));

                if(!headerWrite<Crc_t, CrcSize>(headerBuffer.asSpan().subspan(PackageStartSize
                                                                              + PackageSizeSize
                                                                              + HeaderDataSize),
                                                headerCrc))
                {
                    return false;
                }
            }

            return true;
        }

        using HeaderDataBase = std::
          conditional_t<Config::UseHeaderData, HeaderDataField<HeaderData_t>, NoHeaderDataField>;

        template<typename Span>
        struct FindResult : HeaderDataBase {
            Span body{};
            bool bodyCrcValid{true};
        };

        template<typename Span>
        static constexpr std::optional<FindResult<Span>> findValidPackage(Span& span) {
            while(true) {
                auto const skip = [&]() {
                    span = span.subspan(1);

                    if constexpr(Config::UsePackageStart) {
                        auto const pos = std::find_if(span.begin(), span.end(), [](auto b) {
                            return std::byte{b} == FirstByte;
                        });

                        span = span.subspan(
                          static_cast<std::size_t>(std::distance(span.begin(), pos)));
                    }
                };
                if(HeaderSize + CrcSize > span.size()) { return std::nullopt; }

                if constexpr(Config::UsePackageStart) {
                    PackageStart_t readPackageStart{};

                    copyFixed<PackageStartSize>(std::addressof(readPackageStart), span.data());

                    if(readPackageStart != PackageStart) {
                        skip();
                        continue;
                    }
                }

                HeaderData_t readData{};

                if constexpr(Config::UseHeaderCrc) {
                    Crc_t readHeaderCrc{};
                    if(!headerRead<Crc_t, CrcSize>(
                         readHeaderCrc,
                         span.subspan(PackageStartSize + PackageSizeSize + HeaderDataSize)))
                    {
                        skip();
                        continue;
                    }

                    auto const calcedHeaderCrc
                      = Config::Crc::calc(std::as_bytes(std::span(std::ranges::subrange(
                        span.begin(),
                        std::next(span.begin(),
                                  PackageStartSize + PackageSizeSize + HeaderDataSize)))));

                    if(calcedHeaderCrc != readHeaderCrc) {
                        skip();
                        continue;
                    }

                    if constexpr(Config::UseHeaderData) {
                        if(!headerRead<HeaderData_t, HeaderDataSize>(
                             readData,
                             span.subspan(PackageStartSize + PackageSizeSize)))
                        {
                            skip();
                            continue;
                        }
                    }
                }

                Size_t readBodySize{};
                copyFixed<PackageSizeSize>(std::addressof(readBodySize),
                                           std::next(span.data(), PackageStartSize));

                if(readBodySize > MaxSize || readBodySize < CrcSize) {
                    skip();
                    continue;
                }

                if(HeaderSize + readBodySize > span.size()) { return std::nullopt; }

                if constexpr(Config::UseCrc) {
                    Crc_t readBodyCrc{};
                    if(!headerRead<Crc_t, CrcSize>(
                         readBodyCrc,
                         span.subspan((HeaderSize + readBodySize) - CrcSize)))
                    {
                        skip();
                        continue;
                    }

                    auto const calcedBodyCrc
                      = Config::Crc::calc(std::as_bytes(std::span(std::ranges::subrange(
                        std::next(span.begin(), Config::UseHeaderCrc ? HeaderSize : 0),
                        std::next(span.begin(),
                                  static_cast<std::make_signed_t<std::size_t>>(
                                    (HeaderSize + readBodySize) - CrcSize))))));
                    if(calcedBodyCrc != readBodyCrc) {
                        if constexpr(Config::UseHeaderData) {
                            auto const body = span.subspan(HeaderSize, readBodySize - CrcSize);
                            span            = span.subspan(HeaderSize + readBodySize);
                            FindResult<Span> fr;
                            fr.body         = body;
                            fr.bodyCrcValid = false;
                            if constexpr(Config::UseHeaderData) { fr.headerData = readData; }
                            return fr;
                        } else {
                            skip();
                            continue;
                        }
                    }
                }

                auto const body = span.subspan(HeaderSize, readBodySize - CrcSize);
                span            = span.subspan(HeaderSize + readBodySize);
                FindResult<Span> fr;
                fr.body         = body;
                fr.bodyCrcValid = true;
                if constexpr(Config::UseHeaderData) { fr.headerData = readData; }
                return fr;
            }
        }

    public:
        struct UnpackSuccess : HeaderDataBase {
            std::size_t consumed;
        };

        struct UnpackError : HeaderDataBase {
            UnpackErrorKind kind{UnpackErrorKind::NeedMoreData};
            std::size_t     consumed{0};
        };

        using UnpackReturn_t = std::expected<UnpackSuccess, UnpackError>;

        /// The bytes a frame adds around its body: start, size, header data, header CRC and the
        /// body's CRC, as configured.
        static constexpr std::size_t FrameOverhead{HeaderSize + CrcSize};

        /// The longest frame pack() writes: the configured MaxSize caps the whole frame.
        static constexpr std::size_t MaxFrameSize{MaxSize};

        /// The longest frame pack() writes for any T: its body at max_serialized_size.
        template<typename T>
            requires aglio::HasMaxSerializedSize<T, Size_t>
        static constexpr std::size_t max_packed_size
          = detail::saturatingAdd(FrameOverhead, aglio::max_serialized_size_v<T, Size_t>);

        /// Whether every value of T packs into one frame; false for a T without a worst case.
        template<typename T>
        static constexpr bool always_fits = [] {
            if constexpr(aglio::HasMaxSerializedSize<T, Size_t>) {
                return max_packed_size<T> <= MaxFrameSize;
            } else {
                return false;
            }
        }();

        /// always_fits<T> as a check that says why not: `static_assert(P::template
        /// requireAlwaysFits<T>());` fails naming T and both sizes.
        template<typename T>
        static consteval bool requireAlwaysFits() {
            if constexpr(aglio::HasMaxSerializedSize<T, Size_t>) {
                return detail::FrameHolds<max_packed_size<T>, MaxFrameSize>::value;
            } else {
                return detail::HasNoWorstCase<T>::value;
            }
        }

        template<typename T,
                 typename Buffer>
            requires(!Config::UseHeaderData)
        static constexpr bool pack(Buffer&  buffer,
                                   T const& v) {
            return packImpl(buffer, v, HeaderData_t{});
        }

        template<typename T,
                 typename Buffer>
            requires(Config::UseHeaderData)
        static constexpr bool pack(Buffer&             buffer,
                                   T const&            v,
                                   HeaderData_t const& info) {
            return packImpl(buffer, v, info);
        }

        template<typename T,
                 typename Buffer>
        static constexpr UnpackReturn_t unpack(Buffer& buffer,
                                               T&      v) {
            std::span span{buffer};

            while(true) {
                auto result = findValidPackage(span);
                if(!result) { return std::unexpected(UnpackError{}); }

                auto const consumed = buffer.size() - span.size();

                // find_valid_package only reports a bad body CRC for HeaderData configs; the others
                // resynchronize inside it.
                if constexpr(Config::UseHeaderData) {
                    if(!result->bodyCrcValid) {
                        UnpackError err;
                        err.kind       = UnpackErrorKind::ParseFailure;
                        err.consumed   = consumed;
                        err.headerData = result->headerData;
                        return std::unexpected(err);
                    }
                }

                auto       body = result->body;
                auto const ec   = Serializer::deserialize(body, v);

                if(ec || ec.location != result->body.size()) {
                    if constexpr(Config::UseHeaderData) {
                        UnpackError err;
                        err.kind       = UnpackErrorKind::ParseFailure;
                        err.consumed   = consumed;
                        err.headerData = result->headerData;
                        return std::unexpected(err);
                    } else {
                        continue;
                    }
                }

                UnpackSuccess success;
                success.consumed = consumed;
                if constexpr(Config::UseHeaderData) { success.headerData = result->headerData; }
                return success;
            }
        }

        struct ValidateSuccess : HeaderDataBase {
            std::size_t                consumed;
            std::span<std::byte const> body;
        };

        struct ValidateError : HeaderDataBase {
            UnpackErrorKind kind{UnpackErrorKind::NeedMoreData};
            std::size_t     consumed{0};
        };

        using ValidateReturn_t = std::expected<ValidateSuccess, ValidateError>;

        template<typename Buffer>
            requires(Config::UseHeaderData)
        static constexpr ValidateReturn_t validate(Buffer& buffer) {
            std::span span{buffer};
            auto      result = findValidPackage(span);
            if(!result) { return std::unexpected(ValidateError{}); }

            auto const consumed = buffer.size() - span.size();

            if(!result->bodyCrcValid) {
                ValidateError err;
                err.kind     = UnpackErrorKind::ParseFailure;
                err.consumed = consumed;
                if constexpr(Config::UseHeaderData) { err.headerData = result->headerData; }
                return std::unexpected(err);
            }

            ValidateSuccess success;
            success.consumed = consumed;
            success.body     = result->body;
            if constexpr(Config::UseHeaderData) { success.headerData = result->headerData; }
            return success;
        }
    };

    template<typename SizeT>
    struct Serializer {
        template<typename T,
                 typename Buffer>
        static bool serialize(Buffer&  buffer,
                              T const& v) {
            // A fixed-capacity buffer is written as one window over its free room and resized
            // once, instead of growing per field. On failure its size is restored.
            if constexpr(SizedOnce<Buffer>) {
                auto const start = static_cast<std::size_t>(buffer.size());
                auto const room  = static_cast<std::size_t>(buffer.max_size());
                if(room < start) { return false; }
                bool       ok{};
                auto const write = [&](std::byte* first, std::size_t) {
                    Window const window{
                      std::next(first, static_cast<std::make_signed_t<std::size_t>>(start)),
                      room - start};
                    aglio::DynamicSerializationView sebuff{window};
                    ok = aglio::Serializer<SizeT>::serialize(sebuff, v);
                    return start + (ok ? sebuff.size() : std::size_t{});
                };
                if constexpr(Overwritable<Buffer>) {
                    buffer.resize_and_overwrite(room, write);
                } else {
                    buffer.resize(room);
                    buffer.resize(write(buffer.data(), room));
                }
                return ok;
            } else {
                aglio::DynamicSerializationView sebuff{buffer};
                return aglio::Serializer<SizeT>::serialize(sebuff, v);
            }
        }

        struct ParseError final {
            bool        ec{};
            std::size_t location{};

            operator bool() const { return ec; }
        };

        template<typename T,
                 typename Buffer>
        static ParseError deserialize(Buffer& buffer,
                                      T&      v) {
            aglio::DynamicDeserializationView debuff{buffer};

            if(!aglio::Serializer<SizeT>::deserialize(debuff, v)) {
                return ParseError{.ec = true, .location = 0};
            }
            return ParseError{.ec = false, .location = debuff.size() - debuff.available()};
        }
    };

}   // namespace detail

template<typename CrcType>
struct CrcConfig {
    using Crc                                   = CrcType;
    using Size_t                                = std::uint16_t;
    static constexpr std::uint16_t PackageStart = 0x55AA;
    static constexpr Size_t        MaxSize      = 2048;
};

struct IPConfig {
    using Size_t = std::uint32_t;
};

template<typename Config>
using Packager = detail::Packager<detail::Serializer<typename Config::Size_t>, Config>;

}   // namespace aglio
