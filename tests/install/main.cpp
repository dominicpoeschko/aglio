// Consumes an installed aglio through find_package, which is the one path the main test suite
// cannot cover: it builds against the source tree, so it would not notice a broken export set, a
// missing find_dependency or headers that were never installed.
#define AGLIO_FORMAT_DEFINE_STD
#include <aglio/format.hpp>
//
#define AGLIO_OSTREAM_DEFINE_STD
#include <aglio/ostream.hpp>
#include <aglio/packager.hpp>
#include <aglio/serializer.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Not in an anonymous namespace: glaze's reflection needs the type to have linkage.
struct Config {
    using Size_t = std::uint32_t;
};

struct Message {
    std::uint32_t      id{};
    std::string        name{};
    std::optional<int> value{};

    bool operator==(Message const&) const = default;
};

namespace {

int failures = 0;

void check(bool             ok,
           std::string_view what) {
    if(!ok) {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

}   // namespace

#ifdef AGLIO_USE_ENCHANTUM
enum class Level : std::uint8_t { Low = 1, High = 2 };

struct WithEnum {
    Level level{Level::High};
};
#endif

int main() {
    Message const in{.id = 7, .name = "hello", .value = 42};

    std::vector<std::byte> buffer{};
    check(aglio::Packager<Config>::pack(buffer, in), "pack");

    Message out{};
    check(aglio::Packager<Config>::unpack(buffer, out).has_value(), "unpack");
    check(out == in, "round trip");

    check(std::format("{}", in) == R"({id: 7, name: hello, value: optional(42)})", "std::format");

    std::ostringstream os;
    os << in;
    check(os.str() == R"({id: 7, name: hello, value: optional(42)})", "operator<<");

    // The macro comes from the installed package, so this also checks that the enchantum wiring
    // survived the install rather than only working in the build tree.
#ifdef AGLIO_USE_ENCHANTUM
    check(std::format("{}", WithEnum{}) == R"({level: High})", "enum by name");
#endif

    if(failures != 0) { return 1; }
    std::cout << "installed aglio consumed successfully\n";
    return 0;
}
