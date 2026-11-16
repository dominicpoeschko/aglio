# aglio

Reflection-based serialization for modern C++ — **no macros, no boilerplate**.

aglio turns plain aggregate structs into compact **binary** wire formats, **JSON**, or
human-readable text, using compile-time reflection from
[glaze](https://github.com/stephenberry/glaze). It also ships a small **packet framing**
layer (length prefix, package-start marker, CRC) that's handy for serial links and
embedded protocols.

```cpp
struct Point { int x; int y; };   // that's the whole "schema"
```

## Features

- **Binary serialization** for aggregates, enums, and standard types
  (`optional`, `expected`, `variant`, `chrono::duration`, tuples, ranges, `map`/`set`, …).
- **Packager** — frames messages with an optional start marker, length, CRC, and custom
  header data; `unpack` resynchronizes on a byte stream and reports how much it consumed.
- **JSON** via `to_json` / `from_json` (glaze under the hood).
- **Text output** — `std::formatter`, an `fmt::formatter`, and a `std::ostream`
  `operator<<` for any reflected type, with **enums printed by name**.
- Header-only, works with buffers *or* iostreams - and **in constant evaluation**: a frame built
  by the compiler, a round trip as a `static_assert`.

## Requirements

- A **C++23** compiler (`std::expected`, ranges, …)
- [glaze](https://github.com/stephenberry/glaze) (fetched automatically via CMake)
- [enchantum](https://github.com/ZXShady/enchantum) (fetched automatically; optional, see below)

## Installing

With CMake FetchContent — aglio pulls in glaze itself:

```cmake
include(FetchContent)
FetchContent_Declare(
    aglio
    GIT_REPOSITORY https://github.com/dominicpoeschko/aglio.git
    GIT_TAG        master)
FetchContent_MakeAvailable(aglio)

target_link_libraries(my_app PRIVATE aglio::aglio)
```

Or, after `cmake --install` (requires an installed glaze):

```cmake
find_package(aglio CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE aglio::aglio)
```

## Usage

### Binary serialize / deserialize

`Serializer<Size_t>` picks the integer type used for length prefixes.

```cpp
#include <aglio/serializer.hpp>
#include <aglio/serialization_buffers.hpp>
#include <vector>

struct Point { int x; int y; };

std::vector<std::byte>          bytes;
aglio::DynamicSerializationView out{bytes};
aglio::Serializer<std::uint32_t>::serialize(out, Point{1, 2});

aglio::DynamicDeserializationView in{bytes};
auto point = aglio::Serializer<std::uint32_t>::deserialize<Point>(in);   // std::optional<Point>
```

### Packet framing (start marker + length + CRC)

Describe the wire protocol as a config type, then `pack` / `unpack`:

```cpp
#include <aglio/packager.hpp>

struct Crc16 {
    using type = std::uint16_t;
    static type calc(std::span<std::byte const> data);   // your CRC
};

struct Protocol {
    using Size_t                                = std::uint16_t;
    using Crc                                   = Crc16;
    static constexpr std::uint16_t packageStart = 0x55AA;
};
using Packager = aglio::Packager<Protocol>;

struct Telemetry { float voltage; std::uint16_t rpm; };

std::vector<std::byte> wire;
Packager::pack(wire, Telemetry{3.3f, 1200});

Telemetry out{};
if(auto res = Packager::unpack(wire, out)) {   // std::expected<UnpackSuccess, UnpackError>
    // `out` is filled; res->consumed bytes were used from `wire`
}
```

`pack` appends to the buffer and returns `false` when the package does not fit, leaving the
buffer as it was. The buffer can be a `std::vector<std::byte>` or one of fixed capacity — a
static vector whose `max_size()` and `capacity()` are static and equal, such as
`std::inplace_vector<std::byte, N>`. A fixed one is written in place, without a resize per field;
if it also has `std::string`'s `resize_and_overwrite(n, op)`, nothing in it is filled first.

A type whose serialized size is fixed (`aglio::HasFixedSerializedSize`: numbers, enums, arrays
and structs of them) costs one bounds check however many fields it has. If you specialize
`aglio::TypeSerializer` for a type of your own, specialize `aglio::SerializedSize` with it where the
size is fixed; it has to be the number of bytes the serializer writes, or packing the type fails.

A frame has a configured `maxSize`, so a message that can grow (a static vector, an optional, a
variant) can fail to pack at run time. Its worst case is known at compile time where every
container in it has a static capacity: `aglio::maxSerializedSizeV<T, Size_t>` (and the concept
`aglio::HasMaxSerializedSize`) is the most bytes the serializer writes for any `T` -
`std::vector`, `std::string` and `std::map` have no such bound. The packager adds its framing:

```cpp
static_assert(Packager::frameOverhead == 2 + 2 + 2 + 2);             // start, size, header CRC, body CRC
static_assert(Packager::maxPackedSize<Telemetry> == Packager::frameOverhead + 6);
static_assert(Packager::alwaysFits<Telemetry>);                      // <= Packager::maxFrameSize
static_assert(Packager::requireAlwaysFits<Telemetry>());             // the same, failing with both sizes
```

A serializer of your own whose wire format is not the generic one for its type's shape states its
bound as `static constexpr std::size_t maxSerializedSize`.

### What goes onto the wire, and what comes off it

A number or an enum is written as the bytes it has in memory (the packager asserts a little-endian
target), a `bool` as one byte, 0 or 1. Reading is where a wire can hold anything:

- A number is read through `std::bit_cast` from the bytes, not by copying bytes into it: bytes off
  a wire never were that number, and only `std::bit_cast` says what value they make.
- A `bool` takes any byte other than 0 for `true` - alone and as an element of an array or a
  static vector. No `bool` ends up holding a 2.
- **An enum needs a fixed underlying type** (`enum class E`, `enum E : std::uint8_t`). Without one
  only the values between its enumerators are values of it, reading any other is undefined
  behaviour (and compilers use that: `-fstrict-enums`), and a wire can hold any. Such an enum does
  not compile:

  ```
  error: static assertion failed: aglio: give the enum an underlying type (enum E : std::uint8_t { ... })
  ```

- A floating point type with padding bytes - an x87 `long double`, ten bytes in sixteen - has no
  wire form and does not compile either; use `double`.
- **A duration of a minute or longer travels as a 32-bit count** when a signed integer of 32 bits
  or more counts it in memory. `std::chrono::minutes`, `hours`, `days`, `weeks`, `months` and
  `years` count in a type each standard library chooses - `long` for minutes and hours in libc++,
  which is four bytes on a 32-bit microcontroller and eight on a 64-bit PC, 64 bits everywhere in
  libstdc++ - and without this rule the two ends of a cable disagree about `std::chrono::hours`.
  A count that does not fit 32 bits (hours past 245 000 years) makes `serialize` return `false`.
  Every other duration travels as its own count: `nanoseconds` to `seconds` are 64 bits
  everywhere, and a narrower, an unsigned or a floating point count is one you named.
- What no library can do for you: a `long`, a `std::size_t`, a `wchar_t` or a pointer in a message
  is a different number of bytes on another machine. Use the fixed-width types, and pin a
  message's size in the header both ends compile:
  `static_assert(aglio::serializedSizeV<Message, Size_t> == 46);`
- A contiguous range of numbers (`std::vector<int>`, `std::string`, a static vector of
  `std::byte`) is written with one copy of its bytes, and read with one bounds check and a
  `std::bit_cast` per element - a loop written so that gcc and clang make the one `memcpy` of it
  that it is (also when optimizing for size). Ranges of `bool` and of anything that is not a number
  or enum are read element by element.

Nothing in aglio copies wire bytes into an object or writes one through a view of its bytes.

Use `std::byte` for the buffers. A buffer of `char` or `std::uint8_t` works at run time (its bytes
are looked at as `std::byte`), but not in constant evaluation.

### At compile time

Serializing, deserializing, packing and unpacking work in constant evaluation: a frame can be built
by the compiler, and a protocol's round trip can be a `static_assert`.

```cpp
struct Crc16 {
    using type = std::uint16_t;
    static constexpr type calc(std::span<std::byte const> data);   // constexpr now
};
// Protocol, Packager and Telemetry as above

template<std::size_t N>
constexpr std::array<std::byte, N> packed(auto const& message) {
    std::vector<std::byte> frame;                 // or a static vector of std::byte
    if(!Packager::pack(frame, message) || frame.size() != N) { std::unreachable(); }   // fails to compile
    std::array<std::byte, N> bytes{};
    std::ranges::copy(frame, bytes.begin());
    return bytes;
}

// the frame, CRCs and all, as constant data
inline constexpr auto hello = packed<Packager::frameOverhead + 6>(Telemetry{3.3f, 1200});

static_assert([] {
    Telemetry  out{};
    auto       frame = std::span{hello};
    auto const res   = Packager::unpack(frame, out);
    return res && res->consumed == hello.size() && out.rpm == 1200;
}());
```

What it needs: `std::byte` buffers; a `constexpr` `Crc::calc`; containers that work in constant
evaluation (`std::vector` and `std::string` inside one evaluation, `std::array` and static vectors
anywhere; `std::map` and `std::set` only where the standard library makes them `constexpr`); and
message types without pointers, references or unions. The stream views are run time only. The
bytes are those the same call writes at run time - the test suite compares the two.

### JSON

```cpp
#include <aglio/json.hpp>

std::string text;
aglio::to_json(Point{1, 2}, text);   // {"x":1,"y":2}

Point p{};
aglio::from_json(p, text);
```

### Text output

```cpp
#include <aglio/format.hpp>    // std::formatter<T>
std::print("{}\n", Point{1, 2});    // {x: 1, y: 2}

#include <aglio/fmt.hpp>       // fmt::formatter<T>
fmt::print("{}\n", Point{1, 2});    // {x: 1, y: 2}

#include <aglio/ostream.hpp>   // operator<<
std::cout << Point{1, 2} << '\n';   // {x: 1, y: 2}
```

Enums print by name in all three:

```cpp
enum class Color : std::uint8_t { Red = 1, Green = 2, Blue = 3 };
struct Pixel { Color c; int n; };

fmt::print("{}\n", Pixel{Color::Blue, 7});    // {c: Blue, n: 7}
```

Neither fmt, `std::format` nor iostreams can format an enum on their own, so this comes from
[enchantum](https://github.com/ZXShady/enchantum), whose formatters aglio pulls in. Values with no
matching enumerator fall back to the number, and only values in `[-256, 256]` are reflected by
default — raise `ENCHANTUM_MAX_RANGE` if yours go further.

Turn it off with `-DAGLIO_USE_ENCHANTUM=OFF` to drop the dependency, or if you supply your own enum
formatters: enchantum's specializations match *every* enum, so a second one for the same type is
ambiguous. Without it, printing a type that has an enum member does not compile.

## Tests and fuzzing

```sh
cmake -S tests -B build && cmake --build build && cd build && ctest --output-on-failure
```

`Packager::unpack` and `Packager::validate` consume untrusted bytes, so they are also fuzzed
with libFuzzer (clang only; the target is skipped for compilers without
`-fsanitize=fuzzer`). `ctest` runs a smoke pass over a generated seed corpus with a fixed run
count and seed, so it executes the same inputs on every machine; CI also replays the corpus once
(`-runs=0`) as a blocking check and fuzzes for longer in a job that never blocks. For a longer
session:

```sh
cmake --build build --target fuzz_packager fuzz_corpus_generator
./build/fuzz_corpus_generator build/corpus
./build/fuzz_packager build/corpus tests/corpus_regressions
```

Inputs that once triggered a finding live in `tests/corpus_regressions`. Beyond crashes and
sanitizer reports, the target checks that anything `unpack` accepted survives a repack/unpack
round trip unchanged: re-encoding a decoded value has to produce the same bytes, which is an
oracle that also works for types holding floats, where a fuzzed bit pattern can be a NaN (never
equal to itself) or a signed zero (equal to `+0.0` while the bytes differ). Value equality is
asserted on top for the float-free types.

Because any mutated byte invalidates the CRC of the package it lands in, plain mutation almost
never yields an input the packager accepts. The target therefore installs a
`LLVMFuzzerCustomMutator` that repacks half of the mutated inputs as package bodies behind a
valid header — built with the real `pack`, so the frame layout is never duplicated — and leaves
the other half alone to keep covering resynchronization and header rejection.

`pack` is fuzzed too, into a fixed-capacity buffer whose `max_size()` comes from the input, so
it runs out of room at every byte offset of the header, CRCs and body. The storage is a plain
array, which turns any write past the permitted size into an AddressSanitizer report rather
than a silent pass.

### remote_fmt integration

`src/aglio/remote_fmt.hpp` is guarded by `#if __has_include("remote_fmt/remote_fmt.hpp")`, so it
compiles to nothing unless [remote_fmt](https://github.com/dominicpoeschko/remote_fmt) is
present — which means the default build never checks it. `tests/remote_fmt` is a separate project
that supplies the dependency and does:

```sh
cmake -S tests/remote_fmt -B build-remote-fmt && cmake --build build-remote-fmt
cd build-remote-fmt && ctest --output-on-failure
```

It asserts the generated format string at compile time, and round-trips the same `Types::List`
matrix the `fmt`/`format`/`ostream` suites use through the real `Printer` and parser — remote_fmt
is simply a fourth `Api` in `tests/check_format.hpp`, so all four share one set of golden strings.

The dependency tracks remote_fmt `master`, since the goldens encode its current formatting;
`-DAGLIO_REMOTE_FMT_GIT_TAG=<sha>` freezes a commit instead. To test against an unpushed
remote_fmt, point FetchContent at a working tree:

```sh
cmake -S tests/remote_fmt -B build-remote-fmt \
      -DFETCHCONTENT_SOURCE_DIR_REMOTE_FMT=/path/to/remote_fmt
```

## License

[MIT](LICENSE) © Dominic Poeschko
