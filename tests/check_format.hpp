#pragma once

#include "types.hpp"

#include <aglio/type_descriptor.hpp>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace Test {

enum class Api : std::uint8_t { Format, Fmt, Ostream, RemoteFmt };

// Assertion-free so the Catch2 suite and the standalone one in tests/remote_fmt share the goldens.
template<typename T,
         Api api>
std::string expected_format() {
    if constexpr(api == Api::RemoteFmt) {
        // remote_fmt renders identically to fmt, prefixed per described type with the qualified name.
        // Derived, not spelled out, so a glz::type_name change would move both sides at once -
        // remote_fmt_tests.cpp keeps one literal golden as the anchor against that.
        return "@TYPENAME(" + std::string{glz::type_name<T>} + ")" + expected_format<T, Api::Fmt>();
    } else if constexpr(std::is_same_v<T, Types::Primitive>) {
        if constexpr(api == Api::Ostream) {
            std::string       expected = R"({i8: )";
            std::stringstream ss;
            ss << static_cast<std::int8_t>(-42);
            expected += ss.str();
            expected
              += R"(, i16: -1234, i32: -123456, i64: -12345678901, u8: *, u16: 1234, u32: 123456, u64: 12345678901, f32: 3.14159, f64: 2.71828, flag: true})";
            return expected;
        } else {
            return R"({i8: -42, i16: -1234, i32: -123456, i64: -12345678901, u8: 42, u16: 1234, u32: 123456, u64: 12345678901, f32: 3.14159, f64: 2.71828, flag: true})";
        }
    } else if constexpr(std::is_same_v<T, Types::Container>) {
        return R"({vec: [1, 2, 3, 4, 5], str: Hello, Aglio!, arr: [10, 20, 30, 40, 50]})";
    } else if constexpr(std::is_same_v<T, Types::Associative>) {
        return R"({map: {1: "one", 2: "two", 3: "three"}, int_map: {1: 1, 2: 2, 3: 3}, set: {10, 20, 30, 40}})";
    } else if constexpr(std::is_same_v<T, Types::Wrapper>) {
        return R"({opt_some: optional(42), opt_none: none, var: variant("variant_string"), tup: (100, 3.14, "tuple_str"), pr: (200, "pair_str"), exp_val: expected(99), exp_err: unexpected("error_msg")})";
    } else if constexpr(std::is_same_v<T, Types::WrapperNoFloat>) {
        // std::format and fmt debug-format a nested char ('c'); operator<< writes it raw.
        if constexpr(api == Api::Ostream) {
            return R"({opt_some: optional(42), opt_none: none, var: variant("variant_string"), tup: (100, c, "tuple_str"), pr: (200, "pair_str"), exp_val: expected(99), exp_err: unexpected("error_msg")})";
        } else {
            return R"({opt_some: optional(42), opt_none: none, var: variant("variant_string"), tup: (100, 'c', "tuple_str"), pr: (200, "pair_str"), exp_val: expected(99), exp_err: unexpected("error_msg")})";
        }
    } else if constexpr(std::is_same_v<T, Types::ExpectedVoid>) {
        return R"({ok: expected(), err: unexpected("error_msg")})";
    } else if constexpr(std::is_same_v<T, Types::MoreContainers>) {
        if constexpr(api == Api::Ostream) {
            // operator<< prints [] only for vector and array, {} for other ranges, and uint8_t as a
            // character.
            return R"({deq: {1, 2, 3}, lst: {4, 5}, str_set: {"alpha", "beta"}, str_arr: ["one", "two"], bytes: [A, B], flags: [true, false, true]})";
        } else {
            return R"({deq: [1, 2, 3], lst: [4, 5], str_set: {"alpha", "beta"}, str_arr: ["one", "two"], bytes: [65, 66], flags: [true, false, true]})";
        }
    } else if constexpr(std::is_same_v<T, Types::EmptyContainers>) {
        return R"({vec: [], map: {}, str: , opt_empty_vec: optional([])})";
    } else if constexpr(std::is_same_v<T, Types::NestedWrappers>) {
        return R"({opt_opt: optional(optional(7)), opt_of_empty: optional(none), opt_pair: optional((1, "x")), var_vec: variant([1, 2])})";
    } else if constexpr(std::is_same_v<T, Types::Escapes>) {
        if constexpr(api == Api::Ostream) {
            // operator<< quotes a nested string without escaping its contents.
            return R"({plain: a"b\c, nested: ["a"b", "c\d"]})";
        } else {
            return R"({plain: a"b\c, nested: ["a\"b", "c\\d"]})";
        }
    } else if constexpr(std::is_same_v<T, Types::ChronoEdge>) {
        return R"({secs: 1.5s, negative: -5ms})";
    } else if constexpr(std::is_same_v<T, Types::Chrono>) {
        return R"({ns: 123456789ns, ms: 12345ms, s: 3600s, min: 90min, hr: 24h})";
    } else if constexpr(std::is_same_v<T, Types::Nested>) {
        return R"({nested_vec: [[1, 2, 3], [4, 5], [6, 7, 8, 9]], opt_vec: optional(["opt1", "opt2", "opt3"]), map_of_vecs: {"key1": [1, 2, 3], "key2": [4, 5]}, vec_of_pairs: [(1, "first"), (2, "second"), (3, "third")]})";
    } else if constexpr(std::is_same_v<T, Types::Enum>) {
        // Names, not integers, via enchantum - which is what Api::RemoteFmt always did.
        return R"({color: Blue, status: Active})";
    } else if constexpr(std::is_same_v<T, Types::EnumUnknown>) {
        // No enumerator matches, so the number is printed.
        return R"({unknown: 7, out_of_range: 200})";
    } else if constexpr(std::is_same_v<T, Types::Empty>) {
        return R"({})";
    } else if constexpr(std::is_same_v<T, Types::ContiguousAssociative>) {
        if constexpr(api == Api::Ostream) {
            // enchantum's formatters derive from formatter<string_view>, so a nested name picks up
            // debug quoting; operator<< has no such notion.
            return R"({cset: {Red, Blue}})";
        } else {
            return R"({cset: {"Red", "Blue"}})";
        }
    } else {
        // Fallback for unhandled types, will cause a test failure
        return "TODO IMPLEMENT";
    }
}

// CHECK is a Catch2 macro; the remote_fmt suite has no Catch2 and compares expected_format() itself.
#ifdef CHECK
template<typename T,
         Api api>
void check_format(std::string_view s) {
    CHECK(s == expected_format<T, api>());
}
#endif

}   // namespace Test
