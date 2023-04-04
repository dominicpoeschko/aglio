#pragma once

#include "serialization_buffers.hpp"
#include "serializer.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>

namespace aglio {

namespace detail {
    template<typename Serializer, typename Config>
    struct PackagerWithSize {
    private:
        template<typename T>
        static constexpr std::optional<T> unpack_impl(std::span<std::byte const> buffer) {
            std::optional<T>                  packet;
            aglio::DynamicDeserializationView debuff{buffer};

            packet.emplace();
            if(!Serializer::deserialize(debuff, *packet)) {
                packet.reset();
                return packet;
            }
            return packet;
        }

    public:
        template<typename T, typename Buffer>
        static constexpr bool pack(Buffer& buffer, T const& v) {
            aglio::DynamicSerializationView sebuff{buffer};
            typename Config::Size_t         size{};
            if(!Serializer::serialize(sebuff, size, v)) {
                return false;
            }
            size = static_cast<typename Config::Size_t>(sebuff.size());
            aglio::DynamicSerializationView tmp(buffer);
            if(!Serializer::serialize(tmp, size)) {
                return false;
            }
            buffer.resize(sebuff.size());
            return true;
        }

        template<typename T, typename Buffer>
        static constexpr std::optional<T> unpack(Buffer& buffer) {
            std::optional<T>                  packet;
            typename Config::Size_t           size;
            aglio::DynamicDeserializationView debuff{buffer};
            if(!Serializer::deserialize(debuff, size)) {
                return packet;
            }

            if(size > buffer.size()) {
                return packet;
            }

            packet = unpack_impl<T>(debuff.span());
            buffer.erase(
              buffer.begin(),
              std::next(
                buffer.begin(),
                static_cast<std::make_signed_t<typename Config::Size_t>>(size)));
            return packet;
        }
        template<typename T>
        static constexpr std::optional<T> unpack(std::span<std::byte const> buffer) {
            std::optional<T>                  packet;
            typename Config::Size_t           size;
            aglio::DynamicDeserializationView debuff{buffer};
            if(!Serializer::deserialize(debuff, size)) {
                return packet;
            }

            if(size > buffer.size()) {
                return packet;
            }

            packet = unpack_impl<T>(debuff.span());
            return packet;
        }
    };
    template<typename Serializer, typename Config>
    struct PackagerWithCrc {
        static_assert(
          std::is_trivial_v<decltype(Config::PackageStart)>,
          "start needs to by trivial");
        static_assert(std::is_trivial_v<typename Config::Crc::type>, "crc needs to by trivial");
        static_assert(std::is_trivial_v<typename Config::Size_t>, "size needs to by trivial");

        static constexpr std::size_t StartSize = sizeof(decltype(Config::PackageStart));
        static constexpr std::size_t CrcSize   = sizeof(typename Config::Crc::type);
        static constexpr std::size_t SizeSize  = sizeof(typename Config::Size_t);

        template<typename T, typename Buffer>
        static constexpr bool pack(Buffer& buffer, T const& v) {
            aglio::DynamicSerializationView sebuff{buffer};

            typename Config::Size_t size{};
            if(!Serializer::serialize(sebuff, Config::PackageStart, size, v)) {
                return false;
            }

            size = static_cast<typename Config::Size_t>(sebuff.size() - StartSize);
            aglio::DynamicSerializationView tmp{buffer};
            if(!Serializer::serialize(tmp, Config::PackageStart, size)) {
                return false;
            }
            typename Config::Crc::type const crc = Config::Crc::calc(std::as_bytes(
              std::span{buffer.begin(), std::next(buffer.begin(), size + StartSize)}));
            if(!Serializer::serialize(sebuff, crc)) {
                return false;
            }
            buffer.resize(sebuff.size());
            return true;
        }

        template<typename T, typename Buffer>
        static constexpr std::optional<T> unpack(Buffer& buffer) {
            std::optional<T> packet;
            while(true) {
                packet.reset();
                if((StartSize + SizeSize) > buffer.size()) {
                    return packet;
                }

                std::remove_cv_t<decltype(Config::PackageStart)> start;
                typename Config::Size_t                          size;
                aglio::DynamicDeserializationView                debuff{buffer};
                if(!Serializer::deserialize(debuff, start, size)) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                if(start != Config::PackageStart) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                if(size > Config::MaxSize) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                constexpr std::size_t ExtraSize = CrcSize + StartSize;

                if(size + ExtraSize > buffer.size()) {
                    return packet;
                }

                std::size_t const packet_size = size - SizeSize;

                debuff.skip(packet_size);
                typename Config::Crc::type crc;
                if(!Serializer::deserialize(debuff, crc)) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                typename Config::Crc::type const calcedCrc
                  = Config::Crc::calc(std::as_bytes(std::span{
                    buffer.begin(),
                    std::next(
                      buffer.begin(),
                      static_cast<std::make_signed_t<std::size_t>>(size + StartSize))}));

                if(calcedCrc != crc) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                debuff.unskip(packet_size + CrcSize);
                packet.emplace();

                typename Config::Crc::type crc2;
                if(!Serializer::deserialize(debuff, *packet, crc2)) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                if(calcedCrc != crc2) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                buffer.erase(buffer.begin(), std::next(buffer.begin(), size + ExtraSize));
                return packet;
            }
        }
    };

}   // namespace detail

template<typename Crc_>
struct CrcConfig {
    static constexpr bool isChannelSave         = false;
    using Crc                                   = Crc_;
    using Size_t                                = std::uint16_t;
    static constexpr std::uint16_t PackageStart = 0x55AA;
    static constexpr std::size_t   MaxSize      = 1000;
};

struct IPConfig {
    static constexpr bool isChannelSave = true;
    using Size_t                        = std::uint32_t;
};

template<typename Config>
struct Packager
  : std::conditional_t<
      Config::isChannelSave,
      detail::PackagerWithSize<Serializer, Config>,
      detail::PackagerWithCrc<Serializer, Config>> {};
}   // namespace aglio
