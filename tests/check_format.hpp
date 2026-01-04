#pragma once

#include "types.hpp"

namespace Test {

enum class Api { Format, Fmt, Ostream };

template<typename T,
         Api api>
void check_format(std::string_view s) {
    if constexpr(std::is_same_v<T, Types::Primitive>) {
        if constexpr(api == Api::Ostream) {
            std::string       expected = R"({i8: )";
            std::stringstream ss;
            ss << static_cast<std::int8_t>(-42);
            expected += ss.str();
            expected
              += R"(, i16: -1234, i32: -123456, i64: -12345678901, u8: *, u16: 1234, u32: 123456, u64: 12345678901, f32: 3.14159, f64: 2.71828, flag: true})";
            CHECK(s == expected);
        } else {
            std::string_view const expected
              = R"({i8: -42, i16: -1234, i32: -123456, i64: -12345678901, u8: 42, u16: 1234, u32: 123456, u64: 12345678901, f32: 3.14159, f64: 2.71828, flag: true})";
            CHECK(s == expected);
        }
    } else if constexpr(std::is_same_v<T, Types::Container>) {
        std::string_view const expected
          = R"({vec: [1, 2, 3, 4, 5], str: Hello, Aglio!, arr: [10, 20, 30, 40, 50]})";
        CHECK(s == expected);
    } else if constexpr(std::is_same_v<T, Types::Associative>) {
        std::string_view const expected
          = R"({map: {1: "one", 2: "two", 3: "three"}, int_map: {1: 1, 2: 2, 3: 3}, set: {10, 20, 30, 40}})";
        CHECK(s == expected);
    } else if constexpr(std::is_same_v<T, Types::Wrapper>) {
        std::string_view const expected
          = R"({opt_some: optional(42), opt_none: none, var: variant("variant_string"), tup: (100, 3.14, "tuple_str"), pr: (200, "pair_str")})";
        CHECK(s == expected);
    } else if constexpr(std::is_same_v<T, Types::Chrono>) {
        std::string_view const expected
          = R"({ns: 123456789ns, ms: 12345ms, s: 3600s, min: 90min, hr: 24h})";
        CHECK(s == expected);
    } else if constexpr(std::is_same_v<T, Types::Nested>) {
        std::string_view const expected
          = R"({nested_vec: [[1, 2, 3], [4, 5], [6, 7, 8, 9]], opt_vec: optional(["opt1", "opt2", "opt3"]), map_of_vecs: {"key1": [1, 2, 3], "key2": [4, 5]}, vec_of_pairs: [(1, "first"), (2, "second"), (3, "third")]})";
        CHECK(s == expected);
    } else if constexpr(std::is_same_v<T, Types::Enum>) {
        std::string_view const expected = R"({color: 3, status: 1})";
        CHECK(s == expected);
    } else {
        // Fallback for unhandled types, will cause a test failure
        CHECK(s == "TODO IMPLEMENT");
    }
}

}   // namespace Test
