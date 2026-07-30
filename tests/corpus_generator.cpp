// Seed inputs for fuzz_packager: real packages from Packager::pack, so the fuzzer starts from valid
// wire data instead of guessing a header layout byte by byte.
#include "fuzz_cases.hpp"
#include "types.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

void write(std::filesystem::path const&  path,
           std::vector<std::byte> const& data) {
    std::ofstream file{path, std::ios::binary};
    file.write(reinterpret_cast<char const*>(data.data()),
               static_cast<std::streamsize>(data.size()));
}

// First two bytes mirror LLVMFuzzerTestOneInput. The capacity starts generous so the bounded pack
// succeeds; the fuzzer walks it down.
template<std::size_t Index,
         typename Case>
void write_seeds(std::filesystem::path const& outDir) {
    auto const             value = Types::createDefault<typename Case::Type>();
    std::string const      name  = "seed_" + std::to_string(Index);
    std::vector<std::byte> one{std::byte{static_cast<unsigned char>(Index)}, std::byte{0xFF}};

    if(!Test::fuzz::pack_case<Case>(one, value)) {
        // Some configs cannot pack this type at all, MaxSize below the header size for example. Seed
        // the selector anyway so the case stays reachable from the corpus.
        std::cout << "case " << Index << ": pack failed, writing filler seed\n";
        one.resize(one.size() + 16);
        write(outDir / name, one);
        return;
    }

    write(outDir / name, one);

    // Two packages, so resynchronisation across a boundary is seeded too.
    auto two = one;
    if(Test::fuzz::pack_case<Case>(two, value)) { write(outDir / (name + "_pair"), two); }
}

}   // namespace

int main(int    argc,
         char** argv) {
    if(argc != 2) { return 1; }
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
    std::filesystem::path const outDir{argv[1]};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
    std::filesystem::create_directories(outDir);

    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (write_seeds<Is, std::tuple_element_t<Is, Test::fuzz::Cases>>(outDir), ...);
    }(std::make_index_sequence<Test::fuzz::CaseCount>{});

    return 0;
}
