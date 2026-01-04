# Aglio - Member Name Reflection in C++

[![GitHub stars](https://img.shields.io/github/stars/dominicpoeschko/aglio?style=social)](https://github.com/dominicpoeschko/aglio)
[![GitHub license](https://img.shields.io/github/license/dominicpoeschko/aglio)](https://github.com/dominicpoeschko/aglio/blob/main/LICENSE)
[![CppCon 2021 Lightning Talk](https://img.shields.io/badge/CppCon%202021-Lightning%20Talk-blue)](https://www.youtube.com/watch?v=e5fdPoRVZcg)

**Aglio** is a lightweight C++ library that provides a way to retrieve the names of struct/class members at compile-time using pointers to members. It enables "fun" reflection-like capabilities without runtime overhead, perfect for debugging, serialization, logging, or pretty-printing.

Presented as a Lightning Talk at **CppCon 2021**: ["Fun with Pointers to Members" by Dominic Pöschko](https://www.youtube.com/watch?v=e5fdPoRVZcg).

Static reflection is coming to C++ (eventually), but until then, Aglio offers a clever metaprogramming-based solution to get member names from member pointers.

## Features

- Retrieve member names as `std::string_view` (constexpr-friendly where possible)
- Zero runtime overhead
- Header-only or minimal setup
- Built with modern C++ (C++20 recommended)
- Useful for debugging output, custom serializers, or any meta-programming needs

## Installation

Aglio uses CMake for building (primarily for tests).

```bash
git clone https://github.com/dominicpoeschko/aglio.git
cd aglio
mkdir build && cd build
cmake ..
make
```

To use in your project:
- Include the headers from `src/aglio/`
- Or add as a submodule and link via CMake

## Usage

The core idea is to get the name of a member from a pointer-to-member.

```cpp
#include <aglio/reflection.hpp>  // Adjust include path as needed
#include <iostream>

struct Person {
    int age;
    std::string name;
};

int main() {
    constexpr auto age_ptr = &Person::age;
    constexpr auto name_ptr = &Person::name;

    std::cout << aglio::member_name(age_ptr) << std::endl;  // Outputs: "age"
    std::cout << aglio::member_name(name_ptr) << std::endl; // Outputs: "name"

    // Useful for debugging
    Person p{42, "Alice"};
    std::cout << "age = " << p.*age_ptr << " (" << aglio::member_name(age_ptr) << ")" << std::endl;
}
```

For full struct reflection (enumerating all members), the library may require macro-based registration or specialized techniques shown in the talk.

Watch the [CppCon Lightning Talk](https://www.youtube.com/watch?v=e5fdPoRVZcg) for detailed explanation of the implementation tricks involving pointers to members and template metaprogramming.

## Building and Testing

The repository includes tests:

```bash
cd build
ctest
```

## Contributing

Contributions are welcome! Fork the repo, make your changes, and submit a pull request.

- Report issues: https://github.com/dominicpoeschko/aglio/issues
- Discuss ideas or ask questions via issues

## License

[MIT License](https://github.com/dominicpoeschko/aglio/blob/main/LICENSE) – Free to use, modify, and distribute.

---

*Note: This README is community-generated as the original repository currently lacks detailed documentation. For the exact implementation details, refer to the source code in `src/` and the CppCon talk.*
