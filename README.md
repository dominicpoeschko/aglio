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
  `operator<<` for any reflected type.
- Header-only, `constexpr`-friendly, works with buffers *or* iostreams.

## Requirements

- A **C++23** compiler (`std::expected`, ranges, …)
- [glaze](https://github.com/stephenberry/glaze) (fetched automatically via CMake)

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

## License

[MIT](LICENSE) © Dominic Poeschko
