#pragma once

#include <cstddef>
#include <cstring>

// Forces the small leaf serializers inline, so each field becomes a plain store, not a call.
#if defined(__GNUC__) || defined(__clang__)
    #define AGLIO_INLINE [[gnu::always_inline]] inline
#else
    #define AGLIO_INLINE inline
#endif

// Tells clang that a returned reference or a constructed object refers to this argument, so its lifetime analysis can
// see a dangling use. Nothing on other compilers.
#if defined(__has_cpp_attribute)
    #if __has_cpp_attribute(clang::lifetimebound)
        #define AGLIO_LIFETIMEBOUND [[clang::lifetimebound]]
    #endif
#endif
#ifndef AGLIO_LIFETIMEBOUND
    #define AGLIO_LIFETIMEBOUND
#endif

namespace aglio { namespace detail {

    /// Copies N bytes as loads and stores. The builtin on purpose: with -ffreestanding,
    /// std::memcpy stays a real call even for one byte.
    template<std::size_t N>
    AGLIO_INLINE void copy_fixed(void*       to,
                                 void const* from) noexcept {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memcpy(to, from, N);
#else
        std::memcpy(to, from, N);
#endif
    }

}}   // namespace aglio::detail
