#pragma once

#include <concepts>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <type_traits>

// Forces the small leaf serializers inline, so each field becomes a plain store, not a call.
#if defined(__GNUC__) || defined(__clang__)
    #define AGLIO_INLINE [[gnu::always_inline]] inline
#else
    #define AGLIO_INLINE inline
#endif

// Tells clang that a returned reference or a constructed object refers to this argument, so its lifetime analysis can
// see a dangling use. Nothing on other compilers.
#if __has_cpp_attribute(clang::lifetimebound)
    #define AGLIO_LIFETIMEBOUND [[clang::lifetimebound]]
#else
    #define AGLIO_LIFETIMEBOUND
#endif

// Two pointers that do not reach the same memory - what a std::memcpy call says of its arguments, said of a loop's.
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
    #define AGLIO_RESTRICT __restrict
#else
    #define AGLIO_RESTRICT
#endif

// Something the caller's contract makes true and the optimizer cannot see.
#if __has_cpp_attribute(assume)
    #define AGLIO_ASSUME(...) [[assume(__VA_ARGS__)]]
#else
    #define AGLIO_ASSUME(...)
#endif

namespace aglio { namespace Detail {

    /// What a buffer is made of: std::byte, or a character or integer of one byte.
    template<typename T>
    concept ByteLike
      = std::same_as<std::remove_cv_t<T>, std::byte>
     || (std::is_integral_v<T> && sizeof(T) == 1 && !std::same_as<std::remove_cv_t<T>, bool>);

    /// Bytes of one kind on both sides of a copy. Between two kinds (std::byte and char, say) a
    /// copy would leave to the compiler what a byte of the one kind is as the other: there each
    /// byte is converted.
    template<typename To, typename From>
    concept SameBytes = std::same_as<std::remove_cv_t<To>, std::remove_cv_t<From>>;

    /// n bytes (n > 0), each converted to the destination's kind of byte. A loop the optimizer
    /// makes one copy of: the two sides do not overlap (AGLIO_RESTRICT - what a std::memcpy call
    /// says of its arguments), and it tests at its end (one that tests at its start is not
    /// recognised as a copy when optimizing for size).
    template<ByteLike To,
             ByteLike From>
    constexpr void convertBytes(To* AGLIO_RESTRICT         to,
                                From const* AGLIO_RESTRICT from,
                                std::size_t                n) noexcept {
        std::ptrdiff_t i{};
        do {
            *std::next(to, i) = static_cast<To>(*std::next(from, i));
            ++i;
        } while(i != static_cast<std::ptrdiff_t>(n));
    }

    /// Copies N bytes (N > 0) as loads and stores. The builtin on purpose: with -ffreestanding,
    /// std::memcpy stays a real call even for one byte. Byte by byte (convertBytes) in constant
    /// evaluation, where no copy looks at bytes, and between bytes of two kinds.
    template<std::size_t N,
             ByteLike    To,
             ByteLike    From>
    AGLIO_INLINE constexpr void copyFixed(To*         to,
                                          From const* from) noexcept {
        if !consteval {
            if constexpr(SameBytes<To, From>) {
#if defined(__GNUC__) || defined(__clang__)
                __builtin_memcpy(to, from, N);
#else
                std::memcpy(to, from, N);
#endif
                return;
            }
        }
        convertBytes(to, from, N);
    }

    /// The same for a length (n > 0) known only at run time: one std::memcpy.
    template<ByteLike To,
             ByteLike From>
    constexpr void copyBytes(To*         to,
                             From const* from,
                             std::size_t n) noexcept {
        if !consteval {
            if constexpr(SameBytes<To, From>) {
                std::memcpy(to, from, n);
                return;
            }
        }
        convertBytes(to, from, n);
    }

    /// A buffer's bytes for reading, as std::byte: as they are in a std::byte buffer - the only
    /// kind that works in constant evaluation, where no pointer is cast -, looked at as bytes in
    /// any other. For reading only: written through such a view, a buffer of another kind would
    /// hold what nothing defines.
    template<ByteLike B>
    AGLIO_INLINE constexpr std::byte const* bytePointer(B const* p AGLIO_LIFETIMEBOUND) noexcept {
        if constexpr(std::same_as<std::remove_cv_t<B>, std::byte>) {
            return p;
        } else {
            return static_cast<std::byte const*>(static_cast<void const*>(p));
        }
    }

}}   // namespace aglio::Detail
