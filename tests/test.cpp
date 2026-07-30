// The goldens expect enums printed by name, which needs enchantum's formatters.
#ifndef AGLIO_USE_ENCHANTUM
    #error "the aglio test suite requires AGLIO_USE_ENCHANTUM"
#endif

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
#include "serializer.hpp"
