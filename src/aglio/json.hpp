#pragma once
#include "type_descriptor.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#if __has_include(<nlohmann/json.hpp>)
    #include <nlohmann/json.hpp>
namespace nlohmann {
template<aglio::Described T>
struct adl_serializer<T> {
    static void to_json(nlohmann::json& j, T const& v) {
        using td = aglio::TypeDescriptor<T>;

        constexpr std::size_t argCount = td::N_BaseClasses + td::N_Members;

        if constexpr(argCount != 0) {
            [[maybe_unused]] auto call = [&](auto const&... name_values) {
                [[maybe_unused]] auto subcall = [&](auto const& name_value) {
                    auto const& [name, value] = name_value;
                    if constexpr(requires { std::get<1>(name_value).has_value(); }) {
                        //optional
                        if(value.has_value()) {
                            j[std::string{name}] = value;
                        }
                    } else {
                        j[std::string{name}] = value;
                    }
                };
                (subcall(name_values), ...);
            };

            td::apply_named(call, v);
        } else {
            j = nlohmann::json::object();
        }
    }

    static void from_json(nlohmann::json const& j, T& v) {
        using td = aglio::TypeDescriptor<T>;

        [[maybe_unused]] auto call = [&](auto const&... name_values) {
            [[maybe_unused]] auto subcall = [&](auto& name_value) {
                auto& [name, value] = name_value;
                if constexpr(requires { std::get<1>(name_value).has_value(); }) {
                    //optional
                    if(j.count(std::string(name)) != 0) {
                        j.at(std::string{name}).get_to(value);
                    }
                } else {
                    j.at(std::string{name}).get_to(value);
                }
            };
            (subcall(name_values), ...);
        };

        td::apply_named(call, v);
    }
};

template<typename... Ts>
struct adl_serializer<std::variant<Ts...>> {
    static void to_json(nlohmann::json& j, std::variant<Ts...> const& v) {
        j["index"] = v.index();
        std::visit([&](auto const& vv) { j["value"] = vv; }, v);
    }

    static void from_json(nlohmann::json const& j, std::variant<Ts...>& v) {
        static constexpr std::size_t N{sizeof...(Ts)};

        auto const index = j.at("index").get<std::size_t>();
        if(index >= N) {
            throw 1;
        }
        auto action = [&](auto i) {
            static constexpr std::size_t I = decltype(i)::value;
            if(I == index) {
                std::variant_alternative_t<I, std::variant<Ts...>> vv{};
                j["value"].get_to(vv);
                v = vv;
            }
            return N > I;
        };
        [&]<std::size_t... Ns>(std::index_sequence<Ns...>) {
            (action(std::integral_constant<std::size_t, Ns>{}) && ...);
        }
        (std::make_index_sequence<N>{});
    }
};

template<typename T>
struct adl_serializer<std::optional<T>> {
    static void to_json(nlohmann::json& j, std::optional<T> const& v) {
        if(v.has_value()) {
            j = *v;
        } else {
            j = nullptr;
        }
    }

    static void from_json(nlohmann::json const& j, std::optional<T>& v) {
        if(j.is_null()) {
            v = std::nullopt;
        } else {
            v = j.get<T>();
        }
    }
};

template<typename Rep, typename Period>
struct adl_serializer<std::chrono::duration<Rep, Period>> {
    static void to_json(nlohmann::json& j, std::chrono::duration<Rep, Period> const& v) {
        j = v.count();
    }

    static void from_json(nlohmann::json const& j, std::chrono::duration<Rep, Period>& v) {
        v = std::chrono::duration<Rep, Period>{j.get<Rep>()};
    }
};
}   // namespace nlohmann

namespace aglio {
template<aglio::Described T>
std::string to_json(std::string const& name, T const& v) {
    nlohmann::json j{};
    j[name] = v;
    return j.dump();
}

template<aglio::Described T>
bool from_json(std::string const& json, std::string const& name, T& v) {
    nlohmann::json j{nlohmann::json::parse(json)};
    try {
        j[0][name].get_to(v);
    } catch(...) {
        return false;
    }
    return true;
}
}   // namespace aglio
#endif

namespace aglio {
namespace detail {

    template<typename Buffer>
    constexpr void append(Buffer& buffer, std::string_view v) {
        std::copy(v.begin(), v.end(), std::back_inserter(buffer));
    }

    template<typename Buffer>
    constexpr void append(Buffer& buffer, char v) {
        buffer.push_back(v);
    }

    template<typename Buffer, typename T>
    constexpr void to_json(Buffer& buffer, T const& v);
    template<typename Buffer, typename T>
    constexpr void named(Buffer& buffer, std::string_view name, T const& v);

    template<std::size_t N, typename T>
    constexpr auto to_string_array_impl(T v) {
        std::pair<std::array<char, N>, std::size_t> ret;
        using std::to_chars;
        auto res = to_chars(ret.first.begin(), ret.first.end(), v);
        if(res.ec == std::errc()) {
            ret.second = static_cast<std::size_t>(std::distance(ret.first.begin(), res.ptr));
        } else {
            ret.second = 0;
        }
        return ret;
    }

    template<std::integral T>
    constexpr auto to_string_array(T v) {
        return to_string_array_impl<std::numeric_limits<T>::digits10 * 2 + 2>(v);
    }

    template<std::floating_point T>
    constexpr auto to_string_array(T v) {
        return to_string_array_impl<std::numeric_limits<T>::max_digits10 * 2 + 2>(v);
    }

    template<typename Buffer, char Start, char End>
    struct List {
        static constexpr char Sep{','};
        Buffer&               buffer;
        bool                  first{true};
        constexpr List(Buffer& buffer_) : buffer{buffer_} { append(buffer, Start); }
        constexpr List& operator++() {
            if(!first) {
                append(buffer, Sep);
            }
            first = false;
            return *this;
        }
        constexpr ~List() { append(buffer, End); }
    };
    template<typename Buffer>
    struct Object : List<Buffer, '{', '}'> {
        constexpr Object(Buffer& buffer_) : List<Buffer, '{', '}'>{buffer_} {}
    };
    template<typename Buffer>
    Object(Buffer&) -> Object<Buffer>;

    template<typename Buffer>
    struct Array : List<Buffer, '[', ']'> {
        constexpr Array(Buffer& buffer_) : List<Buffer, '[', ']'>{buffer_} {}
    };
    template<typename Buffer>
    Array(Buffer&) -> Array<Buffer>;

    template<typename Buffer>
    constexpr void to_json_impl(Buffer& buffer, std::string_view const& v) {
        constexpr std::array special_chars{
          std::pair{'\b',  'b'},
          std::pair{'\t',  't'},
          std::pair{'\n',  'n'},
          std::pair{'\f',  'f'},
          std::pair{'\r',  'r'},
          std::pair{ '"',  '"'},
          std::pair{'\\', '\\'}
        };

        append(buffer, '"');
        for(auto const& c : v) {
            if(auto it = std::find_if(
                 special_chars.begin(),
                 special_chars.end(),
                 [&](auto sc) { return sc.first == c; });
               it != special_chars.end())
            {
                append(buffer, '\\');
                append(buffer, it->second);
            } else {
                auto append_nibble = [&](auto cc) {
                    auto nibble = static_cast<decltype(c)>(static_cast<int>(cc & 0x0F));
                    if(nibble < 10) {
                        append(buffer, static_cast<char>(nibble + '0'));
                    } else {
                        append(buffer, static_cast<char>(nibble - 10 + 'a'));
                    }
                };
                if(c < 0x10) {
                    append(buffer, "\\u000");
                    append_nibble(c);
                } else if(c < 0x20) {
                    append(buffer, "\\u001");
                    append_nibble(c);
                } else {
                    append(buffer, c);
                }
            }
        }
        append(buffer, '"');
    }

    template<typename Buffer, std::ranges::range T>
    constexpr void to_json_impl(Buffer& buffer, T const& v) {
        if constexpr(requires { std::string_view{v}; }) {
            to_json_impl(buffer, std::string_view{v});
        } else if constexpr(
          requires { typename T::key_type; }
          && requires { std::string_view{std::declval<typename T::key_type>()}; })
        {
            Object obj{buffer};
            for(auto const& [key, value] : v) {
                ++obj;
                named(buffer, key, value);
            }

        } else {
            Array array{buffer};
            for(auto const& vv : v) {
                ++array;
                to_json(buffer, vv);
            }
        }
    }

    template<typename Buffer, typename T>
    constexpr void to_json_impl(Buffer& buffer, std::optional<T> const& v) {
        if(!v) {
            append(buffer, "null");
        } else {
            to_json(buffer, *v);
        }
    }

    template<typename Buffer, typename... Ts>
    constexpr void to_json_impl(Buffer& buffer, std::variant<Ts...> const& v) {
        Object obj{buffer};
        ++obj;
        named(buffer, "index", v.index());
        ++obj;
        std::visit([&](auto const& vv) { named(buffer, "value", vv); }, v);
    }

    template<typename Buffer, typename T>
        requires requires {
                     {
                         std::tuple_size<T>::value
                         } -> std::convertible_to<std::size_t>;
                 } && (!std::ranges::range<T>)
    constexpr void to_json_impl(Buffer& buffer, T const& v) {
        Array array{buffer};

        auto action = [&](auto const& vv) {
            ++array;
            to_json(buffer, vv);
        };

        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            return (action(get<Is>(v)), ...);
        }
        (std::make_index_sequence<std::tuple_size_v<T>>{});
    }

    template<typename Buffer, typename Rep, typename Period>
    constexpr void to_json_impl(Buffer& buffer, std::chrono::duration<Rep, Period> const& v) {
        to_json(buffer, v.count());
    }
    template<typename Buffer>
    constexpr void to_json_impl(Buffer& buffer, std::byte v) {
        to_json(buffer, std::to_integer<std::uint8_t>(v));
    }
    template<typename Buffer>
    constexpr void to_json_impl(Buffer& buffer, bool v) {
        append(buffer, v ? "true" : "false");
    }
    template<typename Buffer, std::integral T>
    constexpr void to_json_impl(Buffer& buffer, T const& v) {
        auto const [a, n] = to_string_array(v);
        append(buffer, std::string_view{a.data(), n});
    }
    template<typename Buffer, std::floating_point T>
    constexpr void to_json_impl(Buffer& buffer, T const& v) {
        auto const [a, n] = to_string_array(v);
        append(buffer, std::string_view{a.data(), n});
    }

    template<typename Buffer, aglio::Described T>
    constexpr void to_json_impl(Buffer& buffer, T const& v) {
        using td = aglio::TypeDescriptor<T>;

        Object obj{buffer};

        auto call = [&](auto const&... name_values) {
            [[maybe_unused]] auto subcall = [&](auto const& name_value) {
                auto const& [name, value] = name_value;
                ++obj;
                named(buffer, name, value);
            };
            (subcall(name_values), ...);
        };

        td::apply_named(call, v);
    }
    template<typename Buffer, typename T>
    constexpr void to_json(Buffer& buffer, T const& v) {
        to_json_impl(buffer, v);
    }

    template<typename Buffer, typename T>
    constexpr void named(Buffer& buffer, std::string_view name, T const& v) {
        to_json(buffer, name);
        append(buffer, ':');
        to_json(buffer, v);
    }

}   // namespace detail

template<typename Buffer, typename T>
constexpr void to_json(Buffer& buffer, std::string_view name, T const& v) {
    detail::append(buffer, '{');
    detail::named(buffer, name, v);
    detail::append(buffer, '}');
}

}   // namespace aglio
