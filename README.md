# benchmark

A simple CMake Application that uses Google Benchmark

## Pre-requisite - Google Benchmark v1.8.2

"one-liner" installation

``` bash
workdir=$(mktemp -d) && pushd "${workdir}" && git clone --depth 1 https://github.com/google/benchmark.git -b "v1.8.2" && \
        cd benchmark && \
        cmake -E make_directory build && \
        cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release -S . -B build && \
        cmake --build build --config Release -j$(($(nproc) / 3)) && \
        sudo cmake --build build --config Release --target install && \
        popd && \
        rm -fr "${workdir}"
```

## Build and run

### Build

``` bash
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -Bbuild && cmake --build build --config Release --target all -j
```

### Run

``` bash
build/output/Release/benchmark
```