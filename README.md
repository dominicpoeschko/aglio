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
- Header-only, `constexpr`-friendly, works with buffers *or* iostreams.

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
    static constexpr std::uint16_t PackageStart = 0x55AA;
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
`-fsanitize=fuzzer`). `ctest` runs a 10 second smoke pass over a generated seed corpus; for a
longer session:

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
