#include <type_traits>

template<typename Stream,
         typename T>
    requires std::is_enum_v<T>
Stream& operator<<(Stream&  os,
                   T const& v) {
    using underlying = std::underlying_type_t<T>;
    os << static_cast<int>(static_cast<underlying>(v));
    return os;
}

#define AGLIO_OSTREAM_DEFINE_STD
#include <aglio/ostream.hpp>
//
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
#include <catch2/catch_all.hpp>
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
//
#include "fmt.hpp"
#include "format.hpp"
#include "ostream.hpp"
#include "packager.hpp"
