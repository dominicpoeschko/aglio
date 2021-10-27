#pragma once

#include "aglio/serializer.hpp"
#include "aglio/serialization_buffers.hpp"

#include <functional>
#include <optional>

namespace aglio {

namespace detail {
    template<typename Serializer, typename Config>
    struct PackagerWithSize {
        template<typename T, typename Buffer>
        static bool pack(Buffer& buffer, T const& v) {
            aglio::DynamicSerializationView sebuff{buffer};
            typename Config::Size_t           size{};
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
        static std::optional<T> unpack(Buffer& buffer) {
            typename Config::Size_t             size;
            aglio::DynamicDeserializationView debuff{buffer};
            if(!Serializer::deserialize(debuff, size)) {
                return {};
            }

            if(size > buffer.size()) {
                return {};
            }

            T package;
            if(!Serializer::deserialize(debuff, package)) {
                buffer.erase(buffer.begin(), std::next(buffer.begin(), size));
                return {};
            }
            buffer.erase(buffer.begin(), std::next(buffer.begin(), size));
            return package;
        }
    };
    template<typename Serializer, typename Config>
    struct PackagerWithCrc {
        static_assert(
          std::is_trivial_v<decltype(Config::PackageStart)>,
          "start needs to by trivial");

        static constexpr std::size_t StartSize = sizeof(decltype(Config::PackageStart));

        template<typename T, typename Buffer>
        static bool pack(Buffer& buffer, T const& v) {
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
            typename Config::Crc::type const crc
              = Config::Crc::calc(buffer.begin(), std::next(buffer.begin(), size + StartSize));
            if(!Serializer::serialize(sebuff, crc)) {
                return false;
            }
            buffer.resize(sebuff.size());
            return true;
        }

        template<typename T, typename Buffer>
        static std::optional<T> unpack(Buffer& buffer) {
            while(true) {
                if(
                  sizeof(decltype(Config::PackageStart)) + sizeof(typename Config::Size_t)
                  > buffer.size()) {
                    return {};
                }

                std::remove_cv_t<decltype(Config::PackageStart)> start;
                typename Config::Size_t                          size;
                aglio::DynamicDeserializationView              debuff{buffer};
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

                static constexpr std::size_t ExtraSize
                  = sizeof(typename Config::Crc::type) + StartSize;

                if(size + ExtraSize > buffer.size()) {
                    return {};
                }
                T                          packet;
                typename Config::Crc::type crc;
                if(!Serializer::deserialize(debuff, packet, crc)) {
                    buffer.erase(buffer.begin());
                    continue;
                }

                typename Config::Crc::type const calcedCrc
                  = Config::Crc::calc(buffer.begin(), std::next(buffer.begin(), size + StartSize));

                if(calcedCrc != crc) {
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
