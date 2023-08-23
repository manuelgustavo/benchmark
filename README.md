# benchmark

A simple CMake Application that uses Google Benchmark

## Google Benchmark v1.8.2 "one-liner" installation

``` bash
workdir=$(mktemp -d) && cd "${workdir}" && git clone --depth 1 https://github.com/google/benchmark.git -b "v1.8.2" && \
        cd benchmark && \
        cmake -E make_directory build && \
        cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release -S . -B build && \
        cmake --build build --config Release -j$(($(nproc) / 3)) && \
        sudo cmake --build build --config Release --target install && \
        rm -fr "${workdir}"
```
