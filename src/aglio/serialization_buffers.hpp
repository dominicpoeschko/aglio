#pragma once

#include "inline.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>

namespace aglio {
template<typename Buffer>
struct DynamicSerializationView {
private:
    Buffer&     buffer_;
    std::size_t position_{};

public:
    constexpr explicit DynamicSerializationView(Buffer& buffer AGLIO_LIFETIMEBOUND)
      : buffer_{buffer} {}

    constexpr std::size_t size() const { return position_; }

    constexpr std::byte const* data() const { return buffer_.data(); }

    /// A fixed-size field copies with a constant length. Only into a buffer that does not grow;
    /// the others take the insert below.
    template<std::size_t N>
        requires(N != std::dynamic_extent)
    AGLIO_INLINE constexpr bool insert(std::span<std::byte const,
                                                 N> data) {
        if constexpr(N == 0) {
            return true;
        } else if constexpr(!requires { buffer_.resize(1); }) {
            if(N > static_cast<std::size_t>(buffer_.size()) - position_) { return false; }
            detail::copy_fixed<N>(
              std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_)),
              data.data());
            position_ += N;
            return true;
        } else {
            return insert(std::span<std::byte const>{data});
        }
    }

    /// The next n bytes (n > 0) for the caller to write, or nullptr when there is no room.
    AGLIO_INLINE constexpr std::byte* claim(std::size_t n) {
        auto const available = static_cast<std::size_t>(buffer_.size()) - position_;
        if(n > available) {
            if constexpr(requires { buffer_.resize(1); }) {
                auto const newSize = static_cast<std::size_t>(buffer_.size()) + (n - available);
                if constexpr(requires { buffer_.max_size(); }) {
                    if(newSize > static_cast<std::size_t>(buffer_.max_size())) { return nullptr; }
                }
                buffer_.resize(static_cast<decltype(buffer_.size())>(newSize));
            } else {
                return nullptr;
            }
        }
        void* const at
          = std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_));
        position_ += n;
        return static_cast<std::byte*>(at);
    }

    constexpr bool insert(std::span<std::byte const> data) {
        if(data.size_bytes() == 0) { return true; }
        auto available = [&]() { return static_cast<std::size_t>(buffer_.size()) - position_; };
        if(data.size_bytes() > available()) {
            if constexpr(requires { buffer_.resize(1); }) {
                auto const newSize
                  = static_cast<std::size_t>(buffer_.size()) + (data.size_bytes() - available());
                if constexpr(requires { buffer_.max_size(); }) {
                    if(newSize > static_cast<std::size_t>(buffer_.max_size())) { return false; }
                }
                buffer_.resize(static_cast<decltype(buffer_.size())>(newSize));
            } else {
                return false;
            }
        }
        std::memcpy(
          std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_)),
          data.data(),
          data.size_bytes());
        position_ += data.size_bytes();
        return true;
    }
};

template<typename Buffer>
DynamicSerializationView(Buffer&) -> DynamicSerializationView<Buffer>;

template<typename Buffer>
struct DynamicDeserializationView {
private:
    Buffer&     buffer_;
    std::size_t position_{};

public:
    constexpr explicit DynamicDeserializationView(Buffer& buffer AGLIO_LIFETIMEBOUND)
      : buffer_{buffer} {}

    constexpr std::size_t size() const { return buffer_.size(); }

    constexpr auto data() { return buffer_.data(); }

    constexpr void skip(std::size_t length) { position_ += length; }

    constexpr void unskip(std::size_t length) { position_ -= length; }

    constexpr std::size_t available() const {
        return static_cast<std::size_t>(buffer_.size()) - position_;
    }

    constexpr std::span<std::byte const> span() {
        return std::as_bytes(std::span{
          std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_)),
          available()});
    }

    /// The next n bytes (n > 0) for the caller to read, or nullptr when there are fewer.
    AGLIO_INLINE constexpr std::byte const* claim(std::size_t n) {
        if(n > available()) { return nullptr; }
        void const* const at
          = std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_));
        position_ += n;
        return static_cast<std::byte const*>(at);
    }

    /// The counterpart of the serialization view's fixed-size insert.
    template<std::size_t N>
        requires(N != std::dynamic_extent)
    AGLIO_INLINE constexpr bool extract(std::span<std::byte,
                                                  N> data) {
        if constexpr(N == 0) {
            return true;
        } else {
            if(N > available()) { return false; }
            detail::copy_fixed<N>(
              data.data(),
              std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_)));
            position_ += N;
            return true;
        }
    }

    constexpr bool extract(std::span<std::byte> data) {
        if(data.size_bytes() == 0) { return true; }
        if(data.size_bytes() > available()) { return false; }
        std::memcpy(
          data.data(),
          std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_)),
          data.size_bytes());
        position_ += data.size_bytes();
        return true;
    }
};

template<typename Buffer>
DynamicDeserializationView(Buffer&) -> DynamicDeserializationView<Buffer>;

template<typename Stream>
struct StreamSerializationView {
private:
    Stream& stream_;

public:
    constexpr explicit StreamSerializationView(Stream& stream AGLIO_LIFETIMEBOUND)
      : stream_{stream} {}

    constexpr bool insert(std::span<std::byte const> data) {
        if(data.size_bytes() == 0) { return true; }
        stream_.write(reinterpret_cast<char const*>(data.data()),
                      static_cast<std::make_signed_t<std::size_t>>(data.size_bytes()));
        return !stream_.fail();
    }
};

template<typename Stream>
StreamSerializationView(Stream&) -> StreamSerializationView<Stream>;

template<typename Stream>
struct StreamDeserializationView {
private:
    Stream& stream_;

public:
    constexpr explicit StreamDeserializationView(Stream& stream AGLIO_LIFETIMEBOUND)
      : stream_{stream} {}

    constexpr std::size_t size() const { return std::numeric_limits<std::size_t>::max(); }

    constexpr bool extract(std::span<std::byte> data) {
        if(data.size_bytes() == 0) { return true; }

        stream_.read(reinterpret_cast<char*>(data.data()),
                     static_cast<std::make_signed_t<std::size_t>>(data.size_bytes()));

        return !stream_.fail();
    }
};

template<typename Stream>
StreamDeserializationView(Stream&) -> StreamDeserializationView<Stream>;

}   // namespace aglio
