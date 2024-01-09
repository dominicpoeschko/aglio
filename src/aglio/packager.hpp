#pragma once

#include "serialization_buffers.hpp"
#include "serializer.hpp"

#include <cstddef>
#include <functional>
#include <glaze/binary.hpp>
#include <optional>
#include <span>

namespace aglio {

namespace detail {

    template<typename Serializer, typename Config>
    struct Packager {
    private:
        static constexpr bool UseCrc{Config::UseCrc};

        struct NoCrc {
            using type = std::uint8_t;
        };

        using Crc    = std::conditional_t<UseCrc, typename Config::Crc, NoCrc>;
        using Crc_t  = typename Crc::type;
        using Size_t = typename Config::Size_t;

        using PackageStart_t = std::remove_cvref_t<decltype(Config::PackageStart)>;

        static constexpr PackageStart_t PackageStart{Config::PackageStart};
        static constexpr std::byte      FirstByte{PackageStart & 0xFF};
        static constexpr Size_t         MaxSize{Config::MaxSize};

        static_assert(std::is_trivial_v<PackageStart_t>, "start needs to by trivial");
        static_assert(std::is_trivial_v<Crc_t> || UseCrc == false, "crc needs to by trivial");
        static_assert(std::is_trivial_v<Size_t>, "size needs to by trivial");
        static_assert(
          std::numeric_limits<Size_t>::max() >= MaxSize,
          "max size needs to fit into Size_t");
        static_assert(std::endian::native == std::endian::little, "needs little endian");

        static constexpr std::size_t PackageStartSize{sizeof(PackageStart_t)};
        static constexpr std::size_t PackageSizeSize{sizeof(Size_t)};

        static constexpr std::size_t CrcSize{UseCrc ? sizeof(Crc_t) : 0};

        static constexpr std::size_t HeaderSize{PackageStartSize + PackageSizeSize + CrcSize};

        template<typename Buffer>
        struct BufferAdapter {
        private:
            Buffer&           buffer;
            std::size_t const startSize;
            std::size_t       finalizedSize{0};
            bool              finalized{false};

        public:
            explicit BufferAdapter(Buffer& buffer_) : buffer{buffer_}, startSize{buffer.size()} {}

            BufferAdapter(BufferAdapter const&)             = delete;
            BufferAdapter(BufferAdapter&&)                  = delete;
            BufferAdapter& operator==(BufferAdapter const&) = delete;
            BufferAdapter& operator==(BufferAdapter&&)      = delete;

            std::size_t size() const { return buffer.size() - startSize; }

            std::size_t finalized_size() const { return finalizedSize - startSize; }

            void resize(std::size_t newSize) { buffer.resize(newSize + startSize); }

            void finalize() {
                finalizedSize = buffer.size();
                finalized     = true;
            }

            auto data() {
                return std::next(
                  buffer.data(),
                  static_cast<std::make_signed_t<std::size_t>>(startSize));
            }

            auto begin() {
                return std::next(
                  buffer.begin(),
                  static_cast<std::make_signed_t<std::size_t>>(startSize));
            }
            auto end() {
                if(finalized) {
                    return std::next(
                      buffer.begin(),
                      static_cast<std::make_signed_t<std::size_t>>(finalizedSize));
                } else {
                    return buffer.end();
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

    public:
        template<typename T, typename Buffer>
        static constexpr void pack(Buffer& buffer, T const& v) {
            BufferAdapter<Buffer> headerBuffer{buffer};
            headerBuffer.resize(HeaderSize);
            headerBuffer.finalize();

            BufferAdapter<decltype(headerBuffer)> bodyBuffer{headerBuffer};
            Serializer::serialize(bodyBuffer, v);
            bodyBuffer.finalize();

            BufferAdapter<decltype(bodyBuffer)> crcBuffer{bodyBuffer};
            if constexpr(UseCrc) {
                Crc_t const bodyCrc
                  = Crc::calc(std::as_bytes(std::span{bodyBuffer.begin(), bodyBuffer.end()}));

                crcBuffer.resize(CrcSize);
                std::memcpy(crcBuffer.data(), std::addressof(bodyCrc), CrcSize);
            }
            crcBuffer.finalize();

            Size_t const bodySize = bodyBuffer.finalized_size() + crcBuffer.finalized_size();

            std::memcpy(headerBuffer.data(), std::addressof(PackageStart), PackageStartSize);

            std::memcpy(
              std::next(headerBuffer.data(), PackageStartSize),
              std::addressof(bodySize),
              PackageSizeSize);

            if constexpr(UseCrc) {
                Crc_t const headerCrc = Crc::calc(std::as_bytes(std::span{
                  headerBuffer.begin(),
                  std::next(headerBuffer.begin(), PackageStartSize + PackageSizeSize)}));

                std::memcpy(
                  std::next(headerBuffer.data(), PackageStartSize + PackageSizeSize),
                  std::addressof(headerCrc),
                  CrcSize);
            }
        }

        template<typename T, typename Buffer>
        static constexpr std::expected<std::size_t, std::size_t> unpack(Buffer& buffer, T& v) {
            std::span span{buffer};

            while(true) {
                auto skip = [&]() {
                    span = span.subspan(1);

                    auto const pos = std::find_if(span.begin(), span.end(), [](auto b) {
                        return std::byte{b} == FirstByte;
                    });

                    span = span.subspan(static_cast<std::size_t>(std::distance(span.begin(), pos)));
                };

                if(HeaderSize + CrcSize > span.size()) {
                    return std::unexpected{buffer.size() - span.size()};
                }

                PackageStart_t read_packageStart{};

                std::memcpy(std::addressof(read_packageStart), span.data(), PackageSizeSize);

                if(read_packageStart != PackageStart) {
                    skip();
                    continue;
                }

                if constexpr(UseCrc) {
                    Crc_t read_headerCrc{};
                    std::memcpy(
                      std::addressof(read_headerCrc),
                      std::next(span.data(), PackageStartSize + PackageSizeSize),
                      CrcSize);

                    Crc_t const calced_headerCrc = Crc::calc(std::as_bytes(std::span{
                      span.begin(),
                      std::next(span.begin(), PackageStartSize + PackageSizeSize)}));

                    if(calced_headerCrc != read_headerCrc) {
                        skip();
                        continue;
                    }
                }

                Size_t read_bodySize{};
                std::memcpy(
                  std::addressof(read_bodySize),
                  std::next(span.data(), PackageStartSize),
                  PackageSizeSize);

                if(read_bodySize > MaxSize) {
                    skip();
                    continue;
                }

                if(HeaderSize + read_bodySize > span.size()) {
                    return std::unexpected{buffer.size() - span.size()};
                }

                if constexpr(UseCrc) {
                    Crc_t read_bodyCrc{};
                    std::memcpy(
                      std::addressof(read_bodyCrc),
                      std::next(span.data(), (HeaderSize + read_bodySize) - CrcSize),
                      CrcSize);

                    Crc_t const calced_bodyCrc = Crc::calc(std::as_bytes(std::span{
                      std::next(span.begin(), HeaderSize),
                      std::next(span.begin(), (HeaderSize + read_bodySize) - CrcSize)}));

                    if(calced_bodyCrc != read_bodyCrc) {
                        skip();
                        continue;
                    }
                }
                auto s = span.subspan(HeaderSize, read_bodySize - CrcSize);

                auto ec = Serializer::deserialize(s, v);

                if(ec) {
                    skip();
                    continue;
                }

                if(ec.location != (read_bodySize - CrcSize)) {
                    skip();
                    continue;
                }

                span = span.subspan(HeaderSize + read_bodySize);

                return buffer.size() - span.size();
            }
        }
    };

    namespace glz {
        struct Serializer {
            template<typename T, typename Buffer>
            static void serialize(Buffer& buffer, T const& v) {
                ::glz::write_binary(v, buffer);
            }
            template<typename T, typename Buffer>
            static auto deserialize(Buffer& buffer, T& v) {
                return ::glz::read_binary(v, buffer);
            }
        };
    }   // namespace glz

    struct Serializer {
        template<typename T, typename Buffer>
        static void serialize(Buffer& buffer, T const& v) {
            aglio::DynamicSerializationView sebuff{buffer};

            aglio::Serializer::serialize(sebuff, v);
        }

        struct parse_error final {
            bool        ec{};
            std::size_t location{};

            operator bool() const { return ec; }
        };

        template<typename T, typename Buffer>
        static parse_error deserialize(Buffer& buffer, T& v) {
            aglio::DynamicDeserializationView debuff{buffer};

            if(!aglio::Serializer::deserialize(debuff, v)) {
                return parse_error{.ec = true, .location = 0};
            }
            return parse_error{.ec = false, .location = debuff.size() - debuff.available()};
        }
    };

}   // namespace detail

template<typename Crc_>
struct CrcConfig {
    static constexpr bool UseCrc                = true;
    using Crc                                   = Crc_;
    using Size_t                                = std::uint16_t;
    static constexpr std::uint16_t PackageStart = 0x55AA;
    static constexpr Size_t        MaxSize      = 2048;
};

struct IPConfig {
    static constexpr bool UseCrc                = false;
    using Crc                                   = void;
    using Size_t                                = std::uint16_t;
    static constexpr std::uint16_t PackageStart = 0x55AA;
    static constexpr Size_t        MaxSize      = 2048;
};

template<typename Config>
using Packager = detail::Packager<detail::glz::Serializer, Config>;

template<typename Config>
using AglioPackager = detail::Packager<detail::Serializer, Config>;
}   // namespace aglio
