#include "serializer.hpp"
#include "ostream.hpp"

#include <iostream>
#include <vector>
#include <cassert>

struct Foo {
    int         x{};
    int         y{};
    friend auto operator<=>(Foo const&, Foo const&) = default;
};

template<>
struct TypeDescriptor<Foo> 
    : MemberList<&Foo::x, &Foo::y>
{};

int main() {
    std::vector<std::byte> buffer{};

    Foo f0{42, 21};
    if(serialize(buffer, f0)) {
        Foo f1{};
        if(deserialize(buffer, f1)) {
            assert(f0 == f1);
            std::cout << "f0: " << f0 << " f1: " << f1 << '\n';
        }
    }
    return 0;
}

