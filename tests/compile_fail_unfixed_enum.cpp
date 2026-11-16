#include <aglio/serialization_buffers.hpp>
#include <aglio/serializer.hpp>
#include <cstdint>
#include <vector>

// No underlying type: only 0..3 are values of it, and a wire can hold anything.
enum Unfixed { zero, one, two };

int main() {
    std::vector<std::byte>            buf(sizeof(Unfixed));
    aglio::DynamicDeserializationView view{buf};
    Unfixed                           out{};
    return aglio::TypeSerializer<Unfixed, std::uint32_t>::deserialize(out, view) ? 0 : 1;
}
