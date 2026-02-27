#pragma once

#include "serialization_buffers.hpp"
#include "serializer.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <ranges>
#include <span>

namespace aglio {

enum class UnpackErrorKind : std::uint8_t {
    NeedMoreData,
    ParseFailure,
};

namespace detail {

    template<typename T>
    struct HeaderDataField {
        T header_data{};
    };

    struct NoHeaderDataField {};

    template<typename T>
    constexpr bool is_trivial_v
      = std::is_trivially_default_constructible_v<T> && std::is_trivially_copyable_v<T>;

    template<typename Serializer, typename Config_>
    struct Packager {
    private:
        struct Config : Config_ {
            struct NoCrc {
                using type = std::uint8_t;
            };

            static constexpr bool UseCrc = [] {
                if constexpr(requires { typename Config_::Crc; }) {
                    return true;
                } else {
                    return false;
                }
            }();
            static constexpr bool UseHeaderCrc = [] {
                if constexpr(requires { Config_::UseHeaderCrc; }) {
                    return Config_::UseHeaderCrc && UseCrc;
                } else {
                    return UseCrc;
                }
            }();
            static constexpr bool UsePackageStart = [] {
                if constexpr(requires { Config_::PackageStart; }) {
                    return true;
                } else {
                    return false;
                }
            }();
            using Crc                          = decltype([] {
                if constexpr(UseCrc) {
                    return typename Config_::Crc{};
                } else {
                    return NoCrc{};
                }
            }());
            static constexpr auto PackageStart = [] {
                if constexpr(requires { Config_::PackageStart; }) {
                    return Config_::PackageStart;
                } else {
                    return std::uint8_t{};
                }
            }();
            using Size_t                  = typename Config_::Size_t;
            static constexpr auto MaxSize = [] {
                if constexpr(requires { Config_::MaxSize; }) {
                    return Config_::MaxSize;
                } else {
                    return std::numeric_limits<Size_t>::max();
                }
            }();
            static constexpr bool UseHeaderData = [] {
                if constexpr(requires { typename Config_::HeaderData; }) {
                    return true;
                } else {
                    return false;
                }
            }();
            using HeaderData = decltype([] {
                if constexpr(UseHeaderData) {
                    return typename Config_::HeaderData{};
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

        static_assert(is_trivial_v<PackageStart_t> || Config::UsePackageStart == false,
                      "PackageStart must be trivial");
        static_assert(Config::UseCrc == false
                        || aglio::has_fixed_serialized_size<Crc_t,
                                                            Size_t>,
                      "Crc::type must have a fixed serialized size");
        static_assert(Config::UseCrc == false || std::equality_comparable<Crc_t>,
                      "Crc::type must be equality comparable");
        static_assert(is_trivial_v<Size_t>,
                      "Size_t must be trivial");
        static_assert(!Config::UseHeaderData
                        || aglio::has_fixed_serialized_size<HeaderData_t,
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
            std::size_t       finalizedSize{0};
            bool              finalized{false};

        public:
            explicit BufferAdapter(Buffer& buffer_)
              : buffer{buffer_}
              , startSize{buffer.size()} {}

            BufferAdapter(BufferAdapter const&)            = delete;
            BufferAdapter(BufferAdapter&&)                 = delete;
            BufferAdapter& operator=(BufferAdapter const&) = delete;
            BufferAdapter& operator=(BufferAdapter&&)      = delete;

            std::size_t size() const { return buffer.size() - startSize; }

            std::size_t finalized_size() const { return finalizedSize - startSize; }

            void resize(std::size_t newSize) { buffer.resize(newSize + startSize); }

            std::size_t max_size() const {
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
                finalizedSize = buffer.size();
                finalized     = true;
            }

            auto data() {
                return std::next(buffer.data(),
                                 static_cast<std::make_signed_t<std::size_t>>(startSize));
            }

            auto as_span() { return std::span{std::ranges::subrange(begin(), end())}; }

            auto begin() {
                return std::next(buffer.begin(),
                                 static_cast<std::make_signed_t<std::size_t>>(startSize));
            }

            auto end() {
                if(finalized) {
                    return std::next(buffer.begin(),
                                     static_cast<std::make_signed_t<std::size_t>>(finalizedSize));
                } else {
                    return std::next(begin(), static_cast<std::make_signed_t<std::size_t>>(size()));
                }
            }

            bool empty() {
                if(finalized) {
                    return finalizedSize - startSize == 0;
                } else {
                    return buffer.size() <= startSize;
                }
            }
        };

        template<typename T,
                 std::size_t N>
        static bool header_write(std::span<std::byte> buf,
                                 T const&             v) {
            auto                            sub = buf.first<N>();
            aglio::DynamicSerializationView sebuf{sub};
            return aglio::serializer<T, Size_t>::serialize(v, sebuf);
        }

        template<typename T,
                 std::size_t N>
        static bool header_read(T&                         v,
                                std::span<std::byte const> buf) {
            auto                              sub = buf.first<N>();
            aglio::DynamicDeserializationView debuf{sub};
            return aglio::serializer<T, Size_t>::deserialize(v, debuf);
        }

        template<typename T,
                 typename Buffer>
        static constexpr bool packImpl(Buffer&             buffer,
                                       T const&            v,
                                       HeaderData_t const& info) {
            BufferAdapter<Buffer> headerBuffer{buffer};
            if(headerBuffer.max_size() < HeaderSize) { return false; }
            headerBuffer.resize(HeaderSize);

            BufferAdapter<decltype(headerBuffer)> bodyBuffer{headerBuffer};
            if(!Serializer::serialize(bodyBuffer, v)) { return false; }
            bodyBuffer.finalize();

            if constexpr(Config::UseCrc && Config::UseHeaderCrc) {
                BufferAdapter<decltype(bodyBuffer)> crcBuffer{bodyBuffer};
                auto const                          bodyCrc = Config::Crc::calc(std::as_bytes(
                  std::span(std::ranges::subrange(bodyBuffer.begin(), bodyBuffer.end()))));

                if(crcBuffer.max_size() < CrcSize) { return false; }
                crcBuffer.resize(CrcSize);
                if(!header_write<Crc_t, CrcSize>(crcBuffer.as_span(), bodyCrc)) { return false; }
                crcBuffer.finalize();
            }

            Size_t const bodySize
              = static_cast<Config::Size_t>(bodyBuffer.finalized_size() + CrcSize);

            if constexpr(Config::UsePackageStart) {
                std::memcpy(headerBuffer.data(), std::addressof(PackageStart), PackageStartSize);
            }

            std::memcpy(std::next(headerBuffer.data(), PackageStartSize),
                        std::addressof(bodySize),
                        PackageSizeSize);

            if constexpr(Config::UseCrc && !Config::UseHeaderCrc) {
                BufferAdapter<decltype(bodyBuffer)> crcBuffer{bodyBuffer};
                auto const                          bodyCrc = Config::Crc::calc(std::as_bytes(
                  std::span(std::ranges::subrange(headerBuffer.begin(), bodyBuffer.end()))));

                if(crcBuffer.max_size() < CrcSize) { return false; }
                crcBuffer.resize(CrcSize);
                if(!header_write<Crc_t, CrcSize>(crcBuffer.as_span(), bodyCrc)) { return false; }
                crcBuffer.finalize();
            }

            if constexpr(Config::UseHeaderCrc) {
                if constexpr(Config::UseHeaderData) {
                    if(!header_write<HeaderData_t, HeaderDataSize>(
                         headerBuffer.as_span().subspan(PackageStartSize + PackageSizeSize),
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

                if(!header_write<Crc_t, CrcSize>(headerBuffer.as_span().subspan(PackageStartSize
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
            bool body_crc_valid{true};
        };

        template<typename Span>
        static constexpr std::optional<FindResult<Span>> find_valid_package(Span& span) {
            while(true) {
                auto skip = [&]() {
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
                    PackageStart_t read_packageStart{};

                    std::memcpy(std::addressof(read_packageStart), span.data(), PackageStartSize);

                    if(read_packageStart != PackageStart) {
                        skip();
                        continue;
                    }
                }

                HeaderData_t read_data{};

                if constexpr(Config::UseHeaderCrc) {
                    Crc_t read_headerCrc{};
                    if(!header_read<Crc_t, CrcSize>(
                         read_headerCrc,
                         span.subspan(PackageStartSize + PackageSizeSize + HeaderDataSize)))
                    {
                        skip();
                        continue;
                    }

                    auto const calced_headerCrc
                      = Config::Crc::calc(std::as_bytes(std::span(std::ranges::subrange(
                        span.begin(),
                        std::next(span.begin(),
                                  PackageStartSize + PackageSizeSize + HeaderDataSize)))));

                    if(calced_headerCrc != read_headerCrc) {
                        skip();
                        continue;
                    }

                    if constexpr(Config::UseHeaderData) {
                        if(!header_read<HeaderData_t, HeaderDataSize>(
                             read_data,
                             span.subspan(PackageStartSize + PackageSizeSize)))
                        {
                            skip();
                            continue;
                        }
                    }
                }

                Size_t read_bodySize{};
                std::memcpy(std::addressof(read_bodySize),
                            std::next(span.data(), PackageStartSize),
                            PackageSizeSize);

                if(read_bodySize > MaxSize || read_bodySize < CrcSize) {
                    skip();
                    continue;
                }

                if(HeaderSize + read_bodySize > span.size()) { return std::nullopt; }

                if constexpr(Config::UseCrc) {
                    Crc_t read_bodyCrc{};
                    if(!header_read<Crc_t, CrcSize>(
                         read_bodyCrc,
                         span.subspan((HeaderSize + read_bodySize) - CrcSize)))
                    {
                        skip();
                        continue;
                    }

                    auto const calced_bodyCrc
                      = Config::Crc::calc(std::as_bytes(std::span(std::ranges::subrange(
                        std::next(span.begin(), Config::UseHeaderCrc ? HeaderSize : 0),
                        std::next(span.begin(),
                                  static_cast<std::make_signed_t<std::size_t>>(
                                    (HeaderSize + read_bodySize) - CrcSize))))));
                    if(calced_bodyCrc != read_bodyCrc) {
                        if constexpr(Config::UseHeaderData) {
                            auto body = span.subspan(HeaderSize, read_bodySize - CrcSize);
                            span      = span.subspan(HeaderSize + read_bodySize);
                            FindResult<Span> fr;
                            fr.body           = body;
                            fr.body_crc_valid = false;
                            if constexpr(Config::UseHeaderData) { fr.header_data = read_data; }
                            return fr;
                        } else {
                            skip();
                            continue;
                        }
                    }
                }

                auto body = span.subspan(HeaderSize, read_bodySize - CrcSize);
                span      = span.subspan(HeaderSize + read_bodySize);
                FindResult<Span> fr;
                fr.body           = body;
                fr.body_crc_valid = true;
                if constexpr(Config::UseHeaderData) { fr.header_data = read_data; }
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
                auto result = find_valid_package(span);
                if(!result) { return std::unexpected(UnpackError{}); }

                auto const consumed = buffer.size() - span.size();

                if(!result->body_crc_valid) {
                    if constexpr(Config::UseHeaderData) {
                        UnpackError err;
                        err.kind        = UnpackErrorKind::ParseFailure;
                        err.consumed    = consumed;
                        err.header_data = result->header_data;
                        return std::unexpected(err);
                    } else {
                        continue;
                    }
                }

                auto body = result->body;
                auto ec   = Serializer::deserialize(body, v);

                if(ec || ec.location != result->body.size()) {
                    if constexpr(Config::UseHeaderData) {
                        UnpackError err;
                        err.kind        = UnpackErrorKind::ParseFailure;
                        err.consumed    = consumed;
                        err.header_data = result->header_data;
                        return std::unexpected(err);
                    } else {
                        continue;
                    }
                }

                UnpackSuccess success;
                success.consumed = consumed;
                if constexpr(Config::UseHeaderData) { success.header_data = result->header_data; }
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
            auto      result = find_valid_package(span);
            if(!result) { return std::unexpected(ValidateError{}); }

            auto const consumed = buffer.size() - span.size();

            if(!result->body_crc_valid) {
                ValidateError err;
                err.kind     = UnpackErrorKind::ParseFailure;
                err.consumed = consumed;
                if constexpr(Config::UseHeaderData) { err.header_data = result->header_data; }
                return std::unexpected(err);
            }

            ValidateSuccess success;
            success.consumed = consumed;
            success.body     = result->body;
            if constexpr(Config::UseHeaderData) { success.header_data = result->header_data; }
            return success;
        }
    };

    template<typename Size_t>
    struct Serializer {
        template<typename T,
                 typename Buffer>
        static bool serialize(Buffer&  buffer,
                              T const& v) {
            aglio::DynamicSerializationView sebuff{buffer};

            return aglio::Serializer<Size_t>::serialize(sebuff, v);
        }

        struct parse_error final {
            bool        ec{};
            std::size_t location{};

            operator bool() const { return ec; }
        };

        template<typename T,
                 typename Buffer>
        static parse_error deserialize(Buffer& buffer,
                                       T&      v) {
            aglio::DynamicDeserializationView debuff{buffer};

            if(!aglio::Serializer<Size_t>::deserialize(debuff, v)) {
                return parse_error{.ec = true, .location = 0};
            }
            return parse_error{.ec = false, .location = debuff.size() - debuff.available()};
        }
    };

}   // namespace detail

template<typename Crc_>
struct CrcConfig {
    using Crc                                   = Crc_;
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
