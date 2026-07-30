// End-to-end half of the integration test: print with the real Printer, decode with the real parser,
// compare the text. Catches a format string that is well-formed but wrong, which the compile-time
// assertions in format_string_tests.cpp cannot.
//
// remote_fmt.hpp is included directly because src/aglio/remote_fmt.hpp hides its body behind
// __has_include, which would turn a missing dependency into a silently empty test.
#include "check_format.hpp"
#include "types.hpp"

#include <aglio/remote_fmt.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <remote_fmt/parser.hpp>
#include <remote_fmt/remote_fmt.hpp>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace sc::literals;

namespace {

int failures = 0;

struct VectorBackend {
    std::vector<std::byte> memory;

    void write(std::span<std::byte const> data) {
        memory.insert(memory.end(), data.begin(), data.end());
    }
};

// Leaked on purpose: avoids the global-constructor and exit-time-destructor warnings.
std::unordered_map<std::uint16_t,
                   std::string> const&
emptyCatalog() {
    static auto const& catalog = *new std::unordered_map<std::uint16_t, std::string>{};
    return catalog;
}

// Insists the parser consumed everything and discarded nothing.
template<typename... Args>
std::optional<std::string> roundTrip(auto fmtString,
                                     Args&&... args) {
    remote_fmt::Printer<VectorBackend> printer{};
    printer.print(fmtString, std::forward<Args>(args)...);
    auto const& buffer = printer.get_com_backend().memory;

    auto const [message, remaining, discarded]
      = remote_fmt::parse(std::span{buffer}, emptyCatalog(), [](std::string_view error) {
            std::printf("parser error: %.*s\n", static_cast<int>(error.size()), error.data());
        });

    if(!remaining.empty() || discarded != 0) { return std::nullopt; }
    return message;
}

void check(std::optional<std::string> const& actual,
           std::string_view                  expected,
           char const*                       what) {
    if(!actual) {
        std::printf("FAIL: %s: parse failed\n", what);
        ++failures;
        return;
    }
    if(*actual != expected) {
        std::printf("FAIL: %s\n  expected: %.*s\n  actual:   %s\n",
                    what,
                    static_cast<int>(expected.size()),
                    expected.data(),
                    actual->c_str());
        ++failures;
    }
}

}   // namespace

// Not in an anonymous namespace: glaze's reflection needs linkage, and type_name would otherwise
// render as "(anonymous namespace)::Point".
struct Point {
    int x{};
    int y{};
};

struct Empty {};

struct Nested {
    Point p{};
    int   n{};
};

struct WithString {
    std::string name{};
};

struct WithContainer {
    std::vector<int>   vec{};
    std::optional<int> some{};
    std::optional<int> none{};
};

enum class Color : std::uint8_t { Red = 1, Green = 2 };

struct WithEnum {
    Color c{};
    bool  flag{};
};

namespace {

// Same goldens and same matrix as the fmt/format/ostream suites; remote_fmt is a fourth Api.
template<typename T>
void checkTypesEntry() {
    constexpr auto name = glz::type_name<T>;
    check(roundTrip("{}"_sc, Types::createDefault<T>()),
          Test::expected_format<T, Test::Api::RemoteFmt>(),
          std::string{name}.c_str());
}

}   // namespace

int main() {
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (checkTypesEntry<std::tuple_element_t<Is, Types::List>>(), ...);
    }(std::make_index_sequence<std::tuple_size_v<Types::List>>{});

    // Anchor for the loop above, which derives its expectations from glz::type_name and so cannot
    // notice a change in it. Written out in full on purpose.
    check(roundTrip("{}"_sc, Types::createDefault<Types::Enum>()),
          "@TYPENAME(Types::Enum){color: Blue, status: Active}",
          "qualified type name in the marker");

    check(roundTrip("{}"_sc, Point{.x = 1, .y = 2}),
          "@TYPENAME(Point){x: 1, y: 2}",
          "described aggregate");

    check(roundTrip("{}"_sc, Empty{}), "@TYPENAME(Empty){}", "empty aggregate");

    // Has to nest, not flatten.
    check(roundTrip("{}"_sc,
                    Nested{
                      .p = {.x = 3, .y = 4},
                      .n = 5
    }),
          "@TYPENAME(Nested){p: @TYPENAME(Point){x: 3, y: 4}, n: 5}",
          "nested aggregate");

    check(roundTrip("{}"_sc, WithString{.name = "abc"}),
          "@TYPENAME(WithString){name: abc}",
          "string member");

    check(roundTrip("{}"_sc,
                    WithContainer{
                      .vec  = {1, 2, 3},
                      .some = 7,
                      .none = std::nullopt
    }),
          "@TYPENAME(WithContainer){vec: [1, 2, 3], some: optional(7), none: none}",
          "container and optional members");

    // The parser reflects the name with enchantum, so it survives the wire.
    check(roundTrip("{}"_sc, WithEnum{.c = Color::Green, .flag = true}),
          "@TYPENAME(WithEnum){c: Green, flag: true}",
          "enum member");

    // Has to compose inside a larger format string, alongside other arguments.
    check(roundTrip("a {} b {} c"_sc, Point{.x = 1, .y = 2}, 42),
          "a @TYPENAME(Point){x: 1, y: 2} b 42 c",
          "embedded in a larger format string");

    if(failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all remote_fmt integration checks passed\n");
    return 0;
}
