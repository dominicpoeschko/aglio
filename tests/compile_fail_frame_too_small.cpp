#include <aglio/packager.hpp>
#include <array>
#include <cstdint>

struct Config {
    using Size_t                           = std::uint16_t;
    static constexpr std::uint16_t MaxSize = 16;
};

// 2 size bytes of header + 2 count bytes + 20 elements: 24 bytes into a 16-byte frame.
static_assert(aglio::Packager<Config>::requireAlwaysFits<std::array<std::uint8_t,
                                                                    20>>());

int main() {}
