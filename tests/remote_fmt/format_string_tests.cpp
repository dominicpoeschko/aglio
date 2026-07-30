// Compile-time half of the integration test: what format string aglio's remote_fmt::formatter builds.
// Nothing runs, nothing links, a failure is a compile error.
//
// remote_fmt.hpp is included directly because src/aglio/remote_fmt.hpp hides its body behind
// __has_include, which would turn a missing dependency into a silently empty test.
#include <aglio/remote_fmt.hpp>
#include <cstdint>
#include <remote_fmt/remote_fmt.hpp>

// Not in an anonymous namespace: glaze's reflection needs linkage, and type_name would otherwise
// render as "(anonymous namespace)::Point".
struct Point {
    int x{};
    int y{};
};

struct Empty {};

struct OneField {
    std::uint8_t v{};
};

struct Nested {
    Point p{};
    int   n{};
};

// Braces are doubled because the result is itself a format string. These also pin the hand-written
// buffer size in getNamedFmtString(): miscounting either way is ill-formed in a constant expression,
// so the arithmetic cannot drift once this file is compiled at all - which is the point of it.
static_assert(remote_fmt::formatter<Point>::named_fmt_sv == "@TYPENAME(Point){{x: {}, y: {}}}");
static_assert(remote_fmt::formatter<Empty>::named_fmt_sv == "@TYPENAME(Empty){{}}");
static_assert(remote_fmt::formatter<OneField>::named_fmt_sv == "@TYPENAME(OneField){{v: {}}}");
static_assert(remote_fmt::formatter<Nested>::named_fmt_sv == "@TYPENAME(Nested){{p: {}, n: {}}}");
