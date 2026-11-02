#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
#include <cstdint>
#include <vector>

struct WithRef {
    int  value{};
    int& ref;
};

int main() {
    int                             x = 42;
    WithRef                         b{.value = 1, .ref = x};
    std::vector<std::byte>          buf;
    aglio::DynamicSerializationView view{buf};
    aglio::TypeSerializer<WithRef, std::uint32_t>::serialize(b, view);
}
