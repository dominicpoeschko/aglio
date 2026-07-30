#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <list>
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

// optional, variant, tuple, pair, expected
struct Wrapper {
    std::optional<int>                    opt_some{};
    std::optional<int>                    opt_none{};
    std::variant<int, float, std::string> var{};
    std::tuple<int, float, std::string>   tup{};
    std::pair<int, std::string>           pr{};
    std::expected<int, std::string>       exp_val{};
    std::expected<int, std::string>       exp_err{};

    constexpr bool operator==(Wrapper const&) const = default;
};

// Float-free counterpart of Wrapper: variant, tuple and expected appear nowhere else, and a fuzzed
// NaN would break the value-equality oracle.
struct WrapperNoFloat {
    std::optional<int>                   opt_some{};
    std::optional<int>                   opt_none{};
    std::variant<int, char, std::string> var{};
    std::tuple<int, char, std::string>   tup{};
    std::pair<int, std::string>          pr{};
    std::expected<int, std::string>      exp_val{};
    std::expected<int, std::string>      exp_err{};

    constexpr bool operator==(WrapperNoFloat const&) const = default;
};

// void value type: both aglio formatters used to dereference it unconditionally.
struct ExpectedVoid {
    std::expected<void, std::string> ok{};
    std::expected<void, std::string> err{};

    bool operator==(ExpectedVoid const&) const = default;
};

// Ranges that take their own paths through the serializer. The byte values are printable ASCII on
// purpose: operator<< renders std::uint8_t as a character, which the goldens document.
struct MoreContainers {
    std::deque<int>            deq{};
    std::list<int>             lst{};
    std::set<std::string>      str_set{};
    std::array<std::string, 2> str_arr{};
    std::vector<std::uint8_t>  bytes{};
    // Proxy references rather than bool&, which the element deserializer assigns through.
    std::vector<bool> flags{};

    bool operator==(MoreContainers const&) const = default;
};

// Zero-length containers: the size-prefix-of-zero paths, and how an empty range renders.
struct EmptyContainers {
    std::vector<int>                vec{};
    std::map<int, int>              map{};
    std::string                     str{};
    std::optional<std::vector<int>> opt_empty_vec{};

    bool operator==(EmptyContainers const&) const = default;
};

// Wrappers nested in wrappers, plus a variant alternative that is itself a container.
struct NestedWrappers {
    std::optional<std::optional<int>>          opt_opt{};
    std::optional<std::optional<int>>          opt_of_empty{};
    std::optional<std::pair<int, std::string>> opt_pair{};
    std::variant<int, std::vector<int>>        var_vec{};

    bool operator==(NestedWrappers const&) const = default;
};

// Escapes once nested in a container. A quote and a backslash pin the rule and keep the goldens on
// one line; whitespace escapes behave identically across all four APIs.
struct Escapes {
    std::string              plain{};
    std::vector<std::string> nested{};

    bool operator==(Escapes const&) const = default;
};

// Durations that are neither positive nor integral.
struct ChronoEdge {
    std::chrono::duration<double> secs{};
    std::chrono::milliseconds     negative{};

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wfloat-equal"
#endif
    bool operator==(ChronoEdge const&) const = default;
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
};

// Chrono duration types
struct Chrono {
    std::chrono::nanoseconds  ns{};
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

// No matching enumerator, so enchantum falls back to the number; beyond its [-256, 256] default
// range it never even scanned. Color's fixed underlying type is what makes holding these well
// defined.
struct EnumUnknown {
    Color unknown{static_cast<Color>(7)};
    Color out_of_range{static_cast<Color>(200)};

    constexpr auto operator<=>(EnumUnknown const&) const = default;
};

// Empty struct (no members)
struct Empty {
    constexpr auto operator<=>(Empty const&) const = default;
};

// A contiguous (array-backed) set with a trivial element type.
// Before the fix, aglio would silently deserialize zero elements because
// clear() empties the set and the contiguous fast-path then has a zero-length span.
template<typename T, std::size_t Cap>
struct ContiguousSet {
    using key_type       = T;
    using value_type     = T;
    using iterator       = typename std::array<T, Cap>::iterator;
    using const_iterator = typename std::array<T, Cap>::const_iterator;

    std::array<T, Cap> data_{};
    std::size_t        size_{0};

    iterator begin() noexcept { return data_.begin(); }

    const_iterator begin() const noexcept { return data_.begin(); }

    iterator end() noexcept { return std::next(data_.begin(), static_cast<std::ptrdiff_t>(size_)); }

    const_iterator end() const noexcept {
        return std::next(data_.begin(), static_cast<std::ptrdiff_t>(size_));
    }

    std::size_t size() const noexcept { return size_; }

    void clear() noexcept { size_ = 0; }

    std::pair<iterator,
              bool>
    insert(T const& v) noexcept {
        for(std::size_t i = 0; i < size_; ++i) {
            if(data_[i] == v) {
                return {std::next(data_.begin(), static_cast<std::ptrdiff_t>(i)), false};
            }
        }
        if(size_ < Cap) { data_[size_++] = v; }
        return {std::next(data_.begin(), static_cast<std::ptrdiff_t>(size_ - 1)), true};
    }

    bool operator==(ContiguousSet const& o) const noexcept {
        if(size_ != o.size_) { return false; }
        for(std::size_t i = 0; i < size_; ++i) {
            if(data_[i] != o.data_[i]) { return false; }
        }
        return true;
    }
};

struct ContiguousAssociative {
    ContiguousSet<Color, 8> cset{};
    bool                    operator==(ContiguousAssociative const&) const = default;
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
                     .f64  = 2.71828,
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
      .pr       = {200, "pair_str"},
      .exp_val  = 99,
      .exp_err  = std::unexpected{std::string("error_msg")}
    };
}

template<>
WrapperNoFloat createDefault<WrapperNoFloat>() {
    return WrapperNoFloat{
      .opt_some = 42,
      .opt_none = std::nullopt,
      .var      = std::string("variant_string"),
      .tup      = {100, 'c', "tuple_str"},
      .pr       = {200, "pair_str"},
      .exp_val  = 99,
      .exp_err  = std::unexpected{std::string("error_msg")}
    };
}

template<>
ExpectedVoid createDefault<ExpectedVoid>() {
    return ExpectedVoid{.ok = {}, .err = std::unexpected{std::string("error_msg")}};
}

template<>
MoreContainers createDefault<MoreContainers>() {
    return MoreContainers{
      .deq     = {1, 2, 3},
      .lst     = {4, 5},
      .str_set = {"alpha", "beta"},
      .str_arr = {"one", "two"},
      .bytes   = {65, 66},
      .flags   = {true, false, true}
    };
}

template<>
EmptyContainers createDefault<EmptyContainers>() {
    return EmptyContainers{.vec = {}, .map = {}, .str = {}, .opt_empty_vec = std::vector<int>{}};
}

template<>
NestedWrappers createDefault<NestedWrappers>() {
    return NestedWrappers{
      .opt_opt      = std::optional<int>{7},
      .opt_of_empty = std::optional<int>{},
      .opt_pair     = std::pair<int, std::string>{1, "x"},
      .var_vec      = std::vector<int>{1, 2}
    };
}

template<>
Escapes createDefault<Escapes>() {
    return Escapes{
      .plain  = R"(a"b\c)",
      .nested = {R"(a"b)", R"(c\d)"}
    };
}

template<>
ChronoEdge createDefault<ChronoEdge>() {
    return ChronoEdge{.secs     = std::chrono::duration<double>{1.5},
                      .negative = std::chrono::milliseconds{-5}};
}

template<>
Chrono createDefault<Chrono>() {
    return Chrono{.ns  = std::chrono::nanoseconds(123456789),
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

template<>
EnumUnknown createDefault<EnumUnknown>() {
    return EnumUnknown{};
}

template<>
Empty createDefault<Empty>() {
    return Empty{};
}

template<>
ContiguousAssociative createDefault<ContiguousAssociative>() {
    ContiguousAssociative a;
    a.cset.insert(Color::Red);
    a.cset.insert(Color::Blue);
    return a;
}

using List = std::tuple<Primitive,
                        Container,
                        Associative,
                        Wrapper,
                        WrapperNoFloat,
                        ExpectedVoid,
                        MoreContainers,
                        EmptyContainers,
                        NestedWrappers,
                        Escapes,
                        ChronoEdge,
                        Chrono,
                        Nested,
                        Enum,
                        EnumUnknown,
                        Empty,
                        ContiguousAssociative>;

}   // namespace Types
