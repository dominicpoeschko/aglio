#include <aglio/packager.hpp>
#include <cstdint>
#include <vector>

struct Config {
    using Size_t = std::uint16_t;
};

static_assert(aglio::Packager<Config>::requireAlwaysFits<std::vector<std::uint8_t>>());

int main() {}
