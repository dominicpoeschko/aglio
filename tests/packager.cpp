#include <aglio/packager.hpp>
#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

template<typename Stream,
         typename T>
    requires std::is_enum_v<T>
Stream& operator<<(Stream&  os,
                   T const& v) {
    using underlying = std::underlying_type_t<T>;
    return os << static_cast<underlying>(v);
}

#define AGLIO_OSTREAM_DEFINE_STD
#include <aglio/ostream.hpp>

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
#include <catch2/catch_all.hpp>
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
struct MyCrc {
    using type = std::uint32_t;

    static type calc(std::span<std::byte const> data) {
        // A dummy CRC function for testing
        type crc = 0;
        for(auto b : data) { crc += static_cast<type>(b); }
        return crc;
    }
};

namespace Configs {

// Minimal (Size_t only, no CRC, no PackageStart)
struct Minimal {
    using Size_t = std::uint32_t;
};

// PackageStart only (no CRC)
struct SimplePackageStart {
    using Size_t                                = std::uint32_t;
    static constexpr std::uint16_t PackageStart = 0xABCD;
};

// CRC only (UseHeaderCrc defaults to true when CRC present)
struct SimpleCrc {
    using Crc    = MyCrc;
    using Size_t = std::uint32_t;
};

// CRC with UseHeaderCrc explicitly disabled
struct CrcNoHeader {
    using Crc                          = MyCrc;
    using Size_t                       = std::uint32_t;
    static constexpr bool UseHeaderCrc = false;
};

// PackageStart + CRC (implicit UseHeaderCrc=true)
struct Full {
    using Crc                                   = MyCrc;
    using Size_t                                = std::uint32_t;
    static constexpr std::uint16_t PackageStart = 0xABCD;
};

// PackageStart + CRC with UseHeaderCrc=false
struct FullNoHeaderCrc {
    using Crc                                   = MyCrc;
    using Size_t                                = std::uint32_t;
    static constexpr std::uint16_t PackageStart = 0xABCD;
    static constexpr bool          UseHeaderCrc = false;
};

}   // namespace Configs

namespace Types {
// Enum types for testing
enum class Color : uint8_t { Red = 1, Green = 2, Blue = 3 };

enum Status { Unknown = 0, Active = 1, Inactive = 2 };

// All primitive types
struct Primitive {
    int8_t   i8;
    int16_t  i16;
    int32_t  i32;
    int64_t  i64;
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    float    f32;
    double   f64;
    bool     flag;
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
    std::vector<int>   vec;
    std::string        str;
    std::array<int, 5> arr;

    constexpr auto operator<=>(Container const&) const = default;
};

// Map and set containers
struct Associative {
    std::map<int, std::string> map;
    std::set<int>              set;

    constexpr auto operator<=>(Associative const&) const = default;
};

// optional, variant, tuple, pair
struct Wrapper {
    std::optional<int>                    opt_some;
    std::optional<int>                    opt_none;
    std::variant<int, float, std::string> var;
    std::tuple<int, float, std::string>   tup;
    std::pair<int, std::string>           pr;

    constexpr auto operator<=>(Wrapper const&) const = default;
};

// Chrono duration types
struct Chrono {
    std::chrono::nanoseconds  ns;
    std::chrono::microseconds us;
    std::chrono::milliseconds ms;
    std::chrono::seconds      s;
    std::chrono::minutes      min;
    std::chrono::hours        hr;

    constexpr auto operator<=>(Chrono const&) const = default;
};

// Nested/complex structures
struct Nested {
    std::vector<std::vector<int>>            nested_vec;
    std::optional<std::vector<std::string>>  opt_vec;
    std::map<std::string, std::vector<int>>  map_of_vecs;
    std::vector<std::pair<int, std::string>> vec_of_pairs;

    constexpr auto operator<=>(Nested const&) const = default;
};

// Enum types
struct Enum {
    Color  color;
    Status status;

    constexpr auto operator<=>(Enum const&) const = default;
};

}   // namespace Types

template<typename T>
T createIn();

template<typename T>
T createOut();

template<>
Types::Primitive createIn<Types::Primitive>() {
    return Types::Primitive{.i8   = -42,
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
Types::Container createIn<Types::Container>() {
    return Types::Container{
      .vec = { 1,  2,  3,  4,  5},
      .str = "Hello, Aglio!",
      .arr = {10, 20, 30, 40, 50}
    };
}

template<>
Types::Associative createIn<Types::Associative>() {
    return Types::Associative{
      .map = {{1, "one"}, {2, "two"}, {3, "three"}},
      .set = {10, 20, 30, 40}
    };
}

template<>
Types::Wrapper createIn<Types::Wrapper>() {
    return Types::Wrapper{
      .opt_some = 42,
      .opt_none = std::nullopt,
      .var      = std::string("variant_string"),
      .tup      = {100, 3.14f, "tuple_str"},
      .pr       = {200, "pair_str"}
    };
}

template<>
Types::Chrono createIn<Types::Chrono>() {
    return Types::Chrono{.ns  = std::chrono::nanoseconds(123456789),
                         .us  = std::chrono::microseconds(987654),
                         .ms  = std::chrono::milliseconds(12345),
                         .s   = std::chrono::seconds(3600),
                         .min = std::chrono::minutes(90),
                         .hr  = std::chrono::hours(24)};
}

template<>
Types::Nested createIn<Types::Nested>() {
    return Types::Nested{
      .nested_vec   = {{1, 2, 3}, {4, 5}, {6, 7, 8, 9}},
      .opt_vec      = std::vector<std::string>{"opt1", "opt2", "opt3"},
      .map_of_vecs  = {{"key1", {1, 2, 3}}, {"key2", {4, 5}}},
      .vec_of_pairs = {{1, "first"}, {2, "second"}, {3, "third"}}
    };
}

template<>
Types::Enum createIn<Types::Enum>() {
    return Types::Enum{.color = Types::Color::Blue, .status = Types::Active};
}

template<>
Types::Primitive createOut<Types::Primitive>() {
    return Types::Primitive{};
}

template<>
Types::Container createOut<Types::Container>() {
    return Types::Container{};
}

template<>
Types::Associative createOut<Types::Associative>() {
    return Types::Associative{};
}

template<>
Types::Wrapper createOut<Types::Wrapper>() {
    return Types::Wrapper{};
}

template<>
Types::Chrono createOut<Types::Chrono>() {
    return Types::Chrono{};
}

template<>
Types::Nested createOut<Types::Nested>() {
    return Types::Nested{};
}

template<>
Types::Enum createOut<Types::Enum>() {
    return Types::Enum{};
}

template<typename T, typename TTuple>
struct product_one_trait;

template<typename T, typename... Us>
struct product_one_trait<T, std::tuple<Us...>> {
    using type = std::tuple<std::tuple<T, Us>...>;
};

template<typename TTuple1, typename TTuple2>
struct cartesian_product;

template<typename... Ts, typename... Us>
struct cartesian_product<std::tuple<Ts...>, std::tuple<Us...>> {
    using type = decltype(std::tuple_cat(
      std::declval<typename product_one_trait<Ts, std::tuple<Us...>>::type>()...));
};

using TypesList = std::tuple<Types::Primitive,
                             Types::Container,
                             Types::Associative,
                             Types::Wrapper,
                             Types::Chrono,
                             Types::Nested,
                             Types::Enum>;

using ConfigsList = std::tuple<Configs::Minimal,
                               Configs::SimplePackageStart,
                               Configs::SimpleCrc,
                               Configs::CrcNoHeader,
                               Configs::Full,
                               Configs::FullNoHeaderCrc>;

using TestCases = typename cartesian_product<TypesList, ConfigsList>::type;

template<typename Type,
         typename Packager>
void test() {
    std::vector<std::byte> buffer{};

    Type t_in = createIn<Type>();

    Packager::pack(buffer, t_in);
    Type t_out  = createOut<Type>();
    auto result = Packager::unpack(buffer, t_out);

    REQUIRE(result.has_value());
    CHECK(buffer.size() == *result);
    CHECK(t_in == t_out);
}

TEMPLATE_LIST_TEST_CASE("Packager",
                        "[cartesian]",
                        TestCases) {
    using Type   = std::tuple_element_t<0, TestType>;
    using Config = std::tuple_element_t<1, TestType>;

    test<Type, aglio::Packager<Config>>();
}
