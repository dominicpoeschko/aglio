#pragma once
#include "type_descriptor.hpp"

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>

namespace aglio {
template<typename T,
         typename Buffer>
auto to_json(T const& v,
             Buffer&  buffer) {
    return glz::write_json(v, buffer);
}

template<typename T,
         typename Buffer>
auto from_json(T&       v,
               Buffer&& buffer) {
    return glz::read_json(v, std::forward<Buffer>(buffer));
}
}   // namespace aglio
