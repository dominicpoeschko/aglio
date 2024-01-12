#pragma once

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
    constexpr explicit DynamicSerializationView(Buffer& buffer) : buffer_{buffer} {}

    constexpr std::size_t      size() const { return position_; }
    constexpr std::byte const* data() const { return buffer_.data(); }

    constexpr bool insert(std::span<std::byte const> data) {
        if(data.size_bytes() == 0) {
            return true;
        }
        auto available = [&]() { return static_cast<std::size_t>(buffer_.size()) - position_; };
        if(data.size_bytes() > available()) {
            if constexpr(requires { buffer_.resize(1); }) {
                buffer_.resize(static_cast<decltype(buffer_.size())>(
                  static_cast<std::size_t>(buffer_.size()) + (data.size_bytes() - available())));
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
    constexpr explicit DynamicDeserializationView(Buffer& buffer) : buffer_{buffer} {}

    constexpr std::size_t size() const { return buffer_.size(); }
    constexpr std::byte*  data() { return buffer_.data(); }

    constexpr void        skip(std::size_t length) { position_ += length; }
    constexpr void        unskip(std::size_t length) { position_ -= length; }
    constexpr std::size_t available() const {
        return static_cast<std::size_t>(buffer_.size()) - position_;
    }

    constexpr std::span<std::byte const> span() {
        return std::as_bytes(std::span{std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_)), available()});
    }

    constexpr bool extract(std::span<std::byte> data) {
        if(data.size_bytes() == 0) {
            return true;
        }
        if(data.size_bytes() > available()) {
            return false;
        }
        std::memcpy(data.data(), std::next(buffer_.data(), static_cast<std::make_signed_t<std::size_t>>(position_)), data.size_bytes());
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
    constexpr explicit StreamSerializationView(Stream& stream) : stream_{stream} {}

    constexpr bool insert(std::span<std::byte const> data) {
        if(data.size_bytes() == 0) {
            return true;
        }
        stream_.write(reinterpret_cast<char const*>(data.data()), data.size_bytes());
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
    constexpr explicit StreamDeserializationView(Stream& stream) : stream_{stream} {}

    constexpr std::size_t size() const { return std::numeric_limits<std::size_t>::max(); }

    constexpr bool extract(std::span<std::byte> data) {
        if(data.size_bytes() == 0) {
            return true;
        }

        stream_.read(reinterpret_cast<char*>(data.data()), data.size_bytes());

        return !stream_.fail();
    }
};

template<typename Stream>
StreamDeserializationView(Stream&) -> StreamDeserializationView<Stream>;

}   // namespace aglio
