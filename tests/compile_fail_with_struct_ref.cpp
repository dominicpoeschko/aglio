#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
#include <cstdint>
#include <vector>

struct Inner {
    int a{};
    int b{};
};

struct WithStructRef {
    int    value{};
    Inner& ref;
};

int main() {
    Inner                           inner{.a = 1, .b = 2};
    WithStructRef                   b{.value = 1, .ref = inner};
    std::vector<std::byte>          buf;
    aglio::DynamicSerializationView view{buf};
    aglio::TypeSerializer<WithStructRef, std::uint32_t>::serialize(b, view);
}
