#pragma once

#include <array>
#include <cstddef>
#include <cstring>
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
            buffer_.resize(static_cast<decltype(buffer_.size())>(
              static_cast<std::size_t>(buffer_.size()) + (data.size_bytes() - available())));
        }
        std::memcpy(buffer_.data() + position_, data.data(), data.size_bytes());
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
        return std::as_bytes(std::span{buffer_.data() + position_, available()});
    }

    constexpr bool extract(std::span<std::byte> data) {
        if(data.size_bytes() == 0) {
            return true;
        }
        if(data.size_bytes() > available()) {
            return false;
        }
        std::memcpy(data.data(), buffer_.data() + position_, data.size_bytes());
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
        return true;
    }
};

template<typename Stream>
StreamSerializationView(Stream&) -> StreamSerializationView<Stream>;

}   // namespace aglio
