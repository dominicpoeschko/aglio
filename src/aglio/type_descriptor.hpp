#pragma once

#include <glaze/core/reflect.hpp>
#include <glaze/reflection/get_name.hpp>
#include <glaze/reflection/to_tuple.hpp>

namespace aglio {

// A type aglio describes field by field: one glaze reflects, and not a number, an enum or a range
template<typename T>
concept Described = glz::has_reflect<T> && !std::is_integral_v<T> && !std::is_floating_point_v<T>
                 && !std::is_enum_v<T> && !std::ranges::range<T>;

}   // namespace aglio
