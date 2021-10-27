#pragma once

#include <array>
#include <cstring>
#include <cstddef>

namespace aglio {
template<typename Buffer>
struct DynamicSerializationView {
private:
    Buffer&     buffer_;
    std::size_t position_{};

public:
    explicit DynamicSerializationView(Buffer& buffer) : buffer_{buffer} {}

    std::size_t      size() const { return position_; }
    std::byte const* data() const { return buffer_.data(); }

    bool insertBytes(std::byte const* bytes, std::size_t length) {
        auto available = [&]() { return static_cast<std::size_t>(buffer_.size()) - position_; };
        if(length > available()) {
            buffer_.resize(static_cast<decltype(buffer_.size())>(
              static_cast<std::size_t>(buffer_.size()) + (length - available())));
        }
        std::memcpy(buffer_.data() + position_, bytes, length);
        position_ += length;
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
    explicit DynamicDeserializationView(Buffer& buffer) : buffer_{buffer} {}

    void reset(std::size_t length) {
        buffer_.resize(length);
        position_ = 0;
    }

    std::size_t size() const { return buffer_.size(); }
    std::byte*  data() { return buffer_.data(); }
    std::size_t available() const { return static_cast<std::size_t>(buffer_.size()) - position_; }

    bool extractBytes(std::byte* bytes, std::size_t length) {
        if(length > available()) {
            return false;
        }
        std::memcpy(bytes, buffer_.data() + position_, length);
        position_ += length;
        return true;
    }
};

template<typename Buffer>
DynamicDeserializationView(Buffer&) -> DynamicDeserializationView<Buffer>;

template<std::size_t N>
struct FixedSerializationView {
private:
    std::array<std::byte, N>& buffer_;
    std::size_t               position_{};

public:
    constexpr explicit FixedSerializationView(std::array<std::byte, N>& buffer) : buffer_{buffer} {}

    constexpr std::size_t      size() const { return position_; }
    constexpr std::byte const* data() const { return buffer_.data(); }

    constexpr bool insertBytes(std::byte const* bytes, std::size_t length) {
        auto available = [&]() { return buffer_.size() - position_; };
        if(length > available()) {
            return false;
        }
        std::memcpy(buffer_.data() + position_, bytes, length);
        position_ += length;
        return true;
    }
};

template<std::size_t N>
FixedSerializationView(std::array<std::byte, N>&) -> FixedSerializationView<N>;

template<std::size_t N>
struct FixedDeserializationView {
private:
    std::array<std::byte, N>& buffer_;
    std::size_t               position_{};

public:
    constexpr explicit FixedDeserializationView(std::array<std::byte, N>& buffer)
      : buffer_{buffer} {}

    constexpr void reset(std::size_t) { position_ = 0; }

    constexpr std::size_t size() const { return buffer_.size(); }
    constexpr std::byte*  data() { return buffer_.data(); }

    constexpr bool extractBytes(std::byte* bytes, std::size_t length) {
        auto available = [&]() { return buffer_.size() - position_; };
        if(length > available()) {
            return false;
        }
        std::memcpy(bytes, buffer_.data() + position_, length);
        position_ += length;
        return true;
    }
};

template<std::size_t N>
FixedDeserializationView(std::array<std::byte, N>&) -> FixedDeserializationView<N>;
}
