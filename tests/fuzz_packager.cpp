// libFuzzer entry point for Packager::unpack and ::validate, which consume bytes straight off the
// wire: no input may crash, hang, read out of bounds or over-allocate.
//
// Layout: byte 0 picks the case from fuzz_cases.hpp, byte 1 the BoundedBuffer capacity, rest is wire.
#include "fuzz_cases.hpp"

#include <aglio/packager.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace {

// pack() destination whose max_size() comes from the input, making the "no room" returns reachable at
// every offset. Fixed array on purpose: a write past max_size() is then an ASan report, not a pass.
struct BoundedBuffer {
    static constexpr std::size_t Capacity{4096};

    std::array<std::byte, Capacity> storage{};
    std::size_t                     limit{};
    std::size_t                     sz{};

    std::byte* data() { return storage.data(); }

    std::size_t size() const { return sz; }

    std::size_t max_size() const { return limit; }

    auto begin() { return storage.begin(); }

    void resize(std::size_t n) { sz = n; }
};

// pack() destination with a compile-time capacity (max_size() == capacity(), both static).
template<std::size_t N>
struct FixedBuffer {
    std::array<std::byte, N> storage{};
    std::size_t              sz{};

    static constexpr std::size_t capacity() { return N; }

    static constexpr std::size_t max_size() { return N; }

    std::size_t size() const { return sz; }

    std::byte* data() { return storage.data(); }

    auto begin() { return std::span{storage}.first(sz).begin(); }

    auto end() { return std::span{storage}.first(sz).end(); }

    std::byte& operator[](std::size_t i) { return storage[i]; }

    void resize(std::size_t n) {
        if(n > N) { std::abort(); }
        for(std::size_t i = sz; i < n; ++i) {
            storage[i] = std::byte{0x5A};
        }   // not zero: a gap shows
        sz = n;
    }
};

// The same with resize_and_overwrite, which the packager then writes through instead of resize().
template<std::size_t N>
struct RawFixedBuffer : FixedBuffer<N> {
    template<typename Op>
    void resize_and_overwrite(std::size_t n,
                              Op&&        op) {
        if(n > N) { std::abort(); }
        std::size_t const kept = op(this->storage.data(), n);
        if(kept > n) { std::abort(); }
        this->sz = kept;
    }
};

static_assert(aglio::detail::fixed_capacity_v<FixedBuffer<8>>);
static_assert(aglio::detail::overwritable<RawFixedBuffer<8>>);

template<typename Case,
         typename T>
void pack_bounded(T const&    value,
                  std::size_t limit) {
    BoundedBuffer buffer{};
    buffer.limit = limit;
    static_cast<void>(Test::fuzz::pack_case<Case>(buffer, value));
}

// Packs as it arrived: with the observed header data for HeaderData configs, without for the rest.
template<typename Case,
         typename Buffer,
         typename Result>
bool pack_like(Buffer&                    buffer,
               typename Case::Type const& value,
               Result const&              result) {
    using Packager = aglio::Packager<typename Case::Config>;
    if constexpr(requires { Packager::pack(buffer, value, result.header_data); }) {
        return Packager::pack(buffer, value, result.header_data);
    } else {
        return Packager::pack(buffer, value);
    }
}

// Anything accepted must survive a repack/reunpack unchanged, which catches silent truncation and
// aliasing that a crash-only fuzzer sails past.
//
// The oracle is the encoding, not the value: re-encoding a decoded value must give the same bytes.
// That works for floats too, where a fuzzed pattern can be a NaN (never equal to itself) or a signed
// zero (equal to +0.0 while the bytes differ). Value equality is asserted on top where meaningful.
template<typename Case,
         typename Fixed,
         typename Result>
void check_fixed(typename Case::Type const&    value,
                 Result const&                 result,
                 std::vector<std::byte> const& want,
                 std::size_t                   prefix) {
    constexpr std::size_t N = Fixed::capacity();
    Fixed                 fixed{};
    fixed.FixedBuffer<N>::resize(prefix);
    for(std::size_t i = 0; i < prefix; ++i) { fixed.storage[i] = std::byte(0xC0 + i); }
    bool const ok = pack_like<Case>(fixed, value, result);
    if(ok != (prefix + want.size() <= N)) { std::abort(); }
    if(!ok && fixed.size() != prefix) { std::abort(); }
    if(ok) {
        if(fixed.size() != prefix + want.size()) { std::abort(); }
        if(!std::equal(want.begin(), want.end(), std::span{fixed.storage}.subspan(prefix).begin()))
        {
            std::abort();
        }
    }
    for(std::size_t i = 0; i < prefix; ++i) {
        if(fixed.storage[i] != std::byte(0xC0 + i)) { std::abort(); }
    }
}

template<typename Case,
         typename Result>
void check_round_trip(typename Case::Type const& value,
                      Result const&              result) {
    using Packager = aglio::Packager<typename Case::Config>;

    std::vector<std::byte> first{};
    // Repacking can legitimately fail, e.g. when the body no longer fits Config::MaxSize.
    if(!pack_like<Case>(first, value, result)) { return; }

    typename Case::Type again{};
    auto const          againResult = Packager::unpack(first, again);
    if(!againResult) { std::abort(); }
    if(againResult->consumed != first.size()) { std::abort(); }
    if constexpr(requires { result.header_data; }) {
        if(!(againResult->header_data == result.header_data)) { std::abort(); }
    }
    if constexpr(Case::ValueEq) {
        if(!(again == value)) { std::abort(); }
    }

    // Same value, same package.
    std::vector<std::byte> second{};
    if(!pack_like<Case>(second, again, *againResult)) { std::abort(); }
    if(first != second) { std::abort(); }

    // A fixed-capacity buffer gives the same package behind what it already holds, and fits it
    // exactly when the vector's package fits in the room left; a failure leaves it as it was.
    check_fixed<Case, FixedBuffer<4096>>(again, *againResult, first, 3);
    check_fixed<Case, RawFixedBuffer<64>>(again, *againResult, first, 5);
    check_fixed<Case, FixedBuffer<64>>(again, *againResult, first, 5);
    check_fixed<Case, RawFixedBuffer<4096>>(again, *againResult, first, 3);
}

template<typename Case>
void run(std::span<std::byte const> wire,
         std::size_t                packLimit) {
    using Packager = aglio::Packager<typename Case::Config>;
    using Type     = typename Case::Type;

    // A range longer than Size_t can express must be refused, not truncated; with a one byte Size_t
    // any wire over 255 bytes gets there.
    std::vector<std::uint8_t> wireAsRange{};
    wireAsRange.reserve(wire.size());
    for(auto b : wire) { wireAsRange.push_back(static_cast<std::uint8_t>(b)); }
    pack_bounded<Case>(wireAsRange, packLimit);

    auto span = wire;
    while(!span.empty()) {
        Type       value{};
        auto const result = Packager::unpack(span, value);

        std::size_t const consumed = result ? result->consumed : result.error().consumed;

        // Never more bytes than were offered.
        if(consumed > span.size()) { std::abort(); }

        // NeedMoreData: nothing accepted, so re-running the same bytes would loop.
        if(consumed == 0) { break; }

        if(result) {
            check_round_trip<Case>(value, *result);
            pack_bounded<Case>(value, packLimit);
        }

        span = span.subspan(consumed);
    }

    // HeaderData configs only: shares find_valid_package with unpack but hands out the raw body.
    if constexpr(requires { Packager::validate(span); }) {
        auto rest = wire;
        while(!rest.empty()) {
            auto const        result   = Packager::validate(rest);
            std::size_t const consumed = result ? result->consumed : result.error().consumed;
            if(consumed > rest.size()) { std::abort(); }
            if(consumed == 0) { break; }
            if(result) {
                // The body must point inside the buffer handed in.
                auto const offset = result->body.data() - rest.data();
                if(offset < 0
                   || static_cast<std::size_t>(offset) + result->body.size() > rest.size())
                {
                    std::abort();
                }
            }
            rest = rest.subspan(consumed);
        }
    }
}

// Frames `material` as one to three packages via the real pack(), so size field and CRCs are correct
// by construction. False if the config refuses, e.g. body over Config::MaxSize.
template<typename Case>
bool reframe(std::span<std::byte const> material,
             unsigned int               seed,
             std::vector<std::byte>&    out) {
    std::size_t const parts = 1 + ((seed >> 1) % 3);
    std::size_t       offset{};
    for(std::size_t part = 0; part < parts; ++part) {
        auto const remaining = material.size() - offset;
        auto const take      = part + 1 == parts ? remaining : remaining / (parts - part);
        if(!Test::fuzz::pack_case<Case>(out,
                                        Test::fuzz::RawBody{material.subspan(offset, take)},
                                        seed >> 3))
        {
            return false;
        }
        offset += take;
    }
    return true;
}

}   // namespace

extern "C" std::size_t LLVMFuzzerMutate(std::uint8_t* data,
                                        std::size_t   size,
                                        std::size_t   maxSize);

extern "C" std::size_t LLVMFuzzerCustomMutator(std::uint8_t* data,
                                               std::size_t   size,
                                               std::size_t   maxSize,
                                               unsigned int  seed);

// A mutated byte invalidates its package's CRC, so plain mutation almost never yields an input the
// packager accepts. This repacks the mutated bytes as bodies behind a valid header.
extern "C" std::size_t LLVMFuzzerCustomMutator(std::uint8_t* data,
                                               std::size_t   size,
                                               std::size_t   maxSize,
                                               unsigned int  seed) {
    std::size_t const mutated = LLVMFuzzerMutate(data, size, maxSize);
    if(mutated < 3) { return mutated; }

    // Half go back untouched: inputs that do not frame cleanly cover resynchronisation, header
    // rejection and truncation.
    if((seed & 1u) != 0u) { return mutated; }

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    std::span<std::uint8_t const> buffer{data, mutated};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

    auto const selector = static_cast<std::size_t>(buffer[0]) % Test::fuzz::CaseCount;

    // Keep the selector and capacity bytes, reframe the wire behind them.
    std::vector<std::byte> out{};
    out.push_back(static_cast<std::byte>(buffer[0]));
    out.push_back(static_cast<std::byte>(buffer[1]));

    bool framed{};
    Test::fuzz::with_case(selector, [&](auto testCase) {
        framed = reframe<decltype(testCase)>(std::as_bytes(buffer.subspan(2)), seed, out);
    });

    // Fall back to the plain mutation if the reframed one does not fit.
    if(!framed || out.size() > maxSize) { return mutated; }

    std::memcpy(data, out.data(), out.size());
    return out.size();
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data,
                                      std::size_t         size);

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data,
                                      std::size_t         size) {
    if(size < 3) { return 0; }

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    // A raw pointer and size; the two-parameter span construction is the only way to adopt it.
    std::span<std::byte const> input{reinterpret_cast<std::byte const*>(data), size};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

    auto const selector = static_cast<std::size_t>(input[0]) % Test::fuzz::CaseCount;
    // Unscaled: the sweep has to run out of room at every byte offset, and scaling skips half of
    // them. Larger bodies are covered by the unbounded round-trip check.
    auto const packLimit = static_cast<std::size_t>(input[1]);
    auto const wire      = input.subspan(2);

    Test::fuzz::with_case(selector, [wire, packLimit](auto testCase) {
        run<decltype(testCase)>(wire, packLimit);
    });
    return 0;
}
