#pragma once

#include <glaze/core/reflect.hpp>
#include <glaze/reflection/get_name.hpp>
#include <glaze/reflection/to_tuple.hpp>

namespace aglio {

// Alias glaze's reflectable concept for compatibility
template<typename T>
concept Described = glz::has_reflect<T>;

}   // namespace aglio
