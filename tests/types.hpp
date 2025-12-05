#pragma once

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace Types {
// Enum types for testing
enum class Color : std::uint8_t { Red = 1, Green = 2, Blue = 3 };

enum Status { Unknown = 0, Active = 1, Inactive = 2 };

// All primitive types
struct Primitive {
    std::int8_t   i8{};
    std::int16_t  i16{};
    std::int32_t  i32{};
    std::int64_t  i64{};
    std::uint8_t  u8{};
    std::uint16_t u16{};
    std::uint32_t u32{};
    std::uint64_t u64{};
    float         f32{};
    double        f64{};
    bool          flag{};
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wfloat-equal"
#endif
    constexpr auto operator<=>(Primitive const&) const = default;
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
};

// Basic containers (vector, string, array)
struct Container {
    std::vector<int>   vec{};
    std::string        str{};
    std::array<int, 5> arr{};

    constexpr auto operator<=>(Container const&) const = default;
};

// Map and set containers
struct Associative {
    std::map<int, std::string> map{};
    std::map<int, int>         int_map{};
    std::set<int>              set{};

    constexpr auto operator<=>(Associative const&) const = default;
};

// optional, variant, tuple, pair
struct Wrapper {
    std::optional<int>                    opt_some{};
    std::optional<int>                    opt_none{};
    std::variant<int, float, std::string> var{};
    std::tuple<int, float, std::string>   tup{};
    std::pair<int, std::string>           pr{};

    constexpr auto operator<=>(Wrapper const&) const = default;
};

// Chrono duration types
struct Chrono {
    std::chrono::nanoseconds  ns{};
    std::chrono::microseconds us{};
    std::chrono::milliseconds ms{};
    std::chrono::seconds      s{};
    std::chrono::minutes      min{};
    std::chrono::hours        hr{};

    constexpr auto operator<=>(Chrono const&) const = default;
};

// Nested/complex structures
struct Nested {
    std::vector<std::vector<int>>            nested_vec{};
    std::optional<std::vector<std::string>>  opt_vec{};
    std::map<std::string, std::vector<int>>  map_of_vecs{};
    std::vector<std::pair<int, std::string>> vec_of_pairs{};

    constexpr auto operator<=>(Nested const&) const = default;
};

// Enum types
struct Enum {
    Color  color{};
    Status status{};

    constexpr auto operator<=>(Enum const&) const = default;
};

template<typename T>
T createDefault();

template<>
Primitive createDefault<Primitive>() {
    return Primitive{.i8   = -42,
                     .i16  = -1234,
                     .i32  = -123456,
                     .i64  = -12345678901LL,
                     .u8   = 42,
                     .u16  = 1234,
                     .u32  = 123456,
                     .u64  = 12345678901ULL,
                     .f32  = 3.14159f,
                     .f64  = 2.718281828,
                     .flag = true};
}

template<>
Container createDefault<Container>() {
    return Container{
      .vec = { 1,  2,  3,  4,  5},
      .str = "Hello, Aglio!",
      .arr = {10, 20, 30, 40, 50}
    };
}

template<>
Associative createDefault<Associative>() {
    return Associative{
      .map     = {{1, "one"}, {2, "two"}, {3, "three"}},
      .int_map = {{1, 1}, {2, 2}, {3, 3}},
      .set     = {10, 20, 30, 40}
    };
}

template<>
Wrapper createDefault<Wrapper>() {
    return Wrapper{
      .opt_some = 42,
      .opt_none = std::nullopt,
      .var      = std::string("variant_string"),
      .tup      = {100, 3.14f, "tuple_str"},
      .pr       = {200, "pair_str"}
    };
}

template<>
Chrono createDefault<Chrono>() {
    return Chrono{.ns  = std::chrono::nanoseconds(123456789),
                  .us  = std::chrono::microseconds(987654),
                  .ms  = std::chrono::milliseconds(12345),
                  .s   = std::chrono::seconds(3600),
                  .min = std::chrono::minutes(90),
                  .hr  = std::chrono::hours(24)};
}

template<>
Nested createDefault<Nested>() {
    return Nested{
      .nested_vec   = {{1, 2, 3}, {4, 5}, {6, 7, 8, 9}},
      .opt_vec      = std::vector<std::string>{"opt1", "opt2", "opt3"},
      .map_of_vecs  = {{"key1", {1, 2, 3}}, {"key2", {4, 5}}},
      .vec_of_pairs = {{1, "first"}, {2, "second"}, {3, "third"}}
    };
}

template<>
Enum createDefault<Enum>() {
    return Enum{.color = Color::Blue, .status = Active};
}

using List = std::tuple<Primitive, Container, Associative, Wrapper, Chrono, Nested, Enum>;

}   // namespace Types
