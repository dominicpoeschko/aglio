# Local development helpers for aglio.
#
# Builds live in build/<config>/ - one tree per configuration, so switching
# configs never throws away another config's objects. The configs mirror the CI
# matrix in .github/workflows/ci.yml.
#
#   just                    # build + run the tests with the default config
#   just test clang         # same, with clang
#   just test gcc-asan Pack # only tests matching "Pack", asan config
#   just test-all           # every config, like CI does
#   just fuzz-replay        # replay the fuzz corpus once, as CI's blocking fuzz job (clang only)
#   just fuzz               # explore with libFuzzer for a while, as CI's fuzz-explore job
#   just remote-fmt         # the __has_include-guarded remote_fmt integration
#   just tidy               # clang-tidy over the tests and src/ (tidy-remote-fmt: remote_fmt.hpp)
#   just install-check      # consume an installed aglio via find_package
#
# The first configure of any tree fetches cmake_helpers, glaze, enchantum,
# Catch2 and fmt from GitHub, so it needs network. `just --list` shows
# everything.

set shell := ["bash", "-euo", "pipefail", "-c"]

root := source_directory()

self := source_file()   # not justfile(): that names the ROOT justfile when loaded as a module
build_root := root / "build"
jobs := num_cpus()

default_config := "gcc"

# All configurations, in CI order. Used by test-all / build-all.
configs := "gcc gcc-asan clang clang-asan"

default: test

# Print the compilers and cmake flags for a configuration (see the CI matrix)
[private]
flags config:
    #!/usr/bin/env bash
    set -euo pipefail
    # USE_FORCE_FETCH under libc++: Catch2 and fmt must be BUILT here rather than
    # found, or the link mixes them. CI never hits this - its container has no
    # system Catch2 to find - but a developer machine does, and the failure is a
    # wall of undefined std::__ostream_insert / std::_Rb_tree_increment symbols
    # from libstdc++-compiled archives.
    case "{{ config }}" in
        gcc)        echo "gcc g++ " ;;
        gcc-asan)   echo "gcc g++ -DUSE_SANITIZER=address -DUSE_STDLIB_DEBUG=true" ;;
        clang)      echo "clang clang++ " ;;
        clang-asan) echo "clang clang++ -DUSE_STD_LIB=libc++ -DUSE_SANITIZER=address -DUSE_STDLIB_DEBUG=true -DUSE_FORCE_FETCH=ON" ;;
        *)
            echo "unknown config '{{ config }}'; known: {{ configs }}" >&2
            exit 1 ;;
    esac

# Wipe a build tree whose recorded source directory is not this one any more -
# what a moved checkout leaves behind. Without this CMake refuses outright:
# "the current CMakeCache.txt directory ... is different than the directory".
#
# Also wipes a tree configured with another generator: every recipe here drives
# Ninja, and CMake refuses to switch a tree's generator in place.
[private]
guard dir src:
    #!/usr/bin/env bash
    set -euo pipefail
    cache="{{ dir }}/CMakeCache.txt"
    [ -f "$cache" ] || exit 0
    why=""
    home=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")
    [ "$home" = "{{ src }}" ] || why="source moved ($home)"
    gen=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$cache")
    [ "$gen" = Ninja ] || why="generator is '$gen', not Ninja"
    if [ -n "$why" ]; then
        echo "==> wiping {{ dir }}: $why"
        rm -rf "{{ dir }}"
    fi

# Configure build/<config> if it does not exist yet (no-op afterwards)
configure config=default_config: (guard (build_root / config) (root / "tests"))
    #!/usr/bin/env bash
    set -euo pipefail
    dir="{{ build_root }}/{{ config }}"
    if [ -f "$dir/build.ninja" ]; then exit 0; fi
    read -r cc cxx rest < <(just --justfile "{{ self }}" flags "{{ config }}")
    launcher=""
    command -v ccache >/dev/null && launcher="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    env CC="$cc" CXX="$cxx" cmake -S "{{ root }}/tests" -B "$dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        $launcher $rest

# Wipe build/<config> and configure it again
reconfigure config=default_config: (clean config) (configure config)

# Build every test binary of a configuration (incremental)
build config=default_config: (configure config)
    cmake --build "{{ build_root }}/{{ config }}" -j{{ jobs }}

# Build and run the tests; FILTER is a ctest -R regex
test config=default_config filter="": (build config)
    ctest --test-dir "{{ build_root }}/{{ config }}" --output-on-failure -j{{ jobs }} \
        {{ if filter == "" { "" } else { "-R '" + filter + "'" } }}

# Rerun only the tests that failed last time
retest config=default_config:
    ctest --test-dir "{{ build_root }}/{{ config }}" --output-on-failure -j{{ jobs }} --rerun-failed

# List the tests of a configuration
list config=default_config: (configure config)
    ctest --test-dir "{{ build_root }}/{{ config }}" -N

# Build every configuration
build-all:
    #!/usr/bin/env bash
    set -euo pipefail
    for c in {{ configs }}; do
        echo "==> build $c"
        just --justfile "{{ self }}" build "$c"
    done

# Run the tests in every configuration, like CI (continues after failures)
test-all filter="":
    #!/usr/bin/env bash
    set -uo pipefail
    failed=()
    for c in {{ configs }}; do
        echo "==> test $c"
        just --justfile "{{ self }}" test "$c" "{{ filter }}" || failed+=("$c")
    done
    if [ ${#failed[@]} -ne 0 ]; then
        echo "FAILED: ${failed[*]}" >&2
        exit 1
    fi
    echo "all configurations passed"

# Build the packager fuzzer and its generated seed corpus in build/fuzz (clang only).
fuzz-build:
    #!/usr/bin/env bash
    set -euo pipefail
    dir="{{ build_root }}/fuzz"
    just --justfile "{{ self }}" guard "$dir" "{{ root }}/tests"
    if [ ! -f "$dir/build.ninja" ]; then
        env CC=clang CXX=clang++ cmake -S "{{ root }}/tests" -B "$dir" -G Ninja
    fi
    cmake --build "$dir" -j{{ jobs }} --target fuzz_packager fuzz_corpus_generator
    "$dir/fuzz_corpus_generator" "$dir/corpus"

# Run every corpus and regression input once - deterministic, CI's blocking fuzz job.
fuzz-replay: fuzz-build
    "{{ build_root }}/fuzz/fuzz_packager" -runs=0 -timeout=10 \
        "{{ build_root }}/fuzz/corpus" "{{ root }}/tests/corpus_regressions"

# SEED=0 lets libFuzzer pick one (it prints "Seed:"); pass a printed seed to repeat a run's mutations.
# Fuzz the packager for SECONDS - a search, not a test.
fuzz seconds="300" seed="0": fuzz-build
    "{{ build_root }}/fuzz/fuzz_packager" -seed={{ seed }} -max_total_time={{ seconds }} -timeout=10 \
        -print_final_stats=1 "{{ build_root }}/fuzz/corpus" "{{ root }}/tests/corpus_regressions"

# Nothing else compiles the __has_include-guarded src/aglio/remote_fmt.hpp.
# The remote_fmt integration: builds and tests it against upstream master.
remote-fmt: configure-remote-fmt
    cmake --build "{{ build_root }}/remote-fmt" -j{{ jobs }}
    ctest --test-dir "{{ build_root }}/remote-fmt" --output-on-failure -j{{ jobs }}

# Configure build/remote-fmt (clang, with a compile database for tidy-remote-fmt)
[private]
configure-remote-fmt:
    #!/usr/bin/env bash
    set -euo pipefail
    dir="{{ build_root }}/remote-fmt"
    just --justfile "{{ self }}" guard "$dir" "{{ root }}/tests/remote_fmt"
    env CC=clang CXX=clang++ cmake -S "{{ root }}/tests/remote_fmt" -B "$dir" -G Ninja \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# A broken export set or an unshipped header shows up here and nowhere else.
# Consume aglio as a user does: install it, then find_package it. ENCHANTUM ON/OFF.
install-check enchantum="ON":
    #!/usr/bin/env bash
    set -euo pipefail
    prefix="{{ build_root }}/prefix-{{ enchantum }}"
    rm -rf "$prefix" "{{ build_root }}/install-{{ enchantum }}" "{{ build_root }}/consumer-{{ enchantum }}"
    env CC=clang CXX=clang++ cmake -S "{{ root }}" -B "{{ build_root }}/install-{{ enchantum }}" -G Ninja \
        -DAGLIO_INSTALL=ON -DAGLIO_USE_ENCHANTUM={{ enchantum }} \
        -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_INSTALL_PREFIX="$prefix"
    cmake --install "{{ build_root }}/install-{{ enchantum }}"
    env CC=clang CXX=clang++ cmake -S "{{ root }}/tests/install" -B "{{ build_root }}/consumer-{{ enchantum }}" \
        -G Ninja -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build "{{ build_root }}/consumer-{{ enchantum }}" -j{{ jobs }}
    ctest --test-dir "{{ build_root }}/consumer-{{ enchantum }}" --output-on-failure
    run-clang-tidy -p "{{ build_root }}/consumer-{{ enchantum }}" -quiet -warnings-as-errors='*' \
        '{{ root }}/tests/install/main\.cpp$'

# Remove build/<config>
clean config=default_config:
    rm -rf "{{ build_root }}/{{ config }}"

# Remove every build directory
clean-all:
    rm -rf "{{ build_root }}"

# --- clang-tidy ----------------------------------------------------------------

# run-clang-tidy with this repository's .clang-tidy over the clang tree's test sources (the compile-fail ones are
# meant not to compile); any finding in src/ or tests/ fails. Extra arguments go to run-clang-tidy: `just tidy -fix`,
# `just tidy -checks=<glob>`.
# clang-tidy over the clang host tree (exit 0 clean, 1 findings)
[positional-arguments]
tidy *args: (configure "clang")
    run-clang-tidy -p "{{ build_root }}/clang" -quiet -j{{ jobs }} -warnings-as-errors='*' "$@" \
        '{{ root }}/tests/(?!compile_fail_)[^/]*\.cpp$'

# clang-tidy over the remote_fmt integration (tests/remote_fmt and src/aglio/remote_fmt.hpp, which the clang tree
# never compiles). The anchored header filter keeps remote_fmt's own src/ out wherever it was fetched to.
# clang-tidy over the remote_fmt tree (exit 0 clean, 1 findings)
[positional-arguments]
tidy-remote-fmt *args: configure-remote-fmt
    run-clang-tidy -p "{{ build_root }}/remote-fmt" -quiet -j{{ jobs }} -warnings-as-errors='*' \
        -header-filter='^{{ root }}/(src|tests)/' "$@" '{{ root }}/tests/remote_fmt/[^/]*\.cpp$'

# For a caller that runs clang-tidy over several trees:
# The configured clang trees `tidy` and `tidy-remote-fmt` read, if there are any
tidy-trees:
    @[ ! -f "{{ build_root }}/clang/compile_commands.json" ] || echo "{{ build_root }}/clang"
    @[ ! -f "{{ build_root }}/remote-fmt/compile_commands.json" ] || echo "{{ build_root }}/remote-fmt"
