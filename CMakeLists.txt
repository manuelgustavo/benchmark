cmake_minimum_required(VERSION 3.15.0)
project(benchmark VERSION 0.1.0)

set(CPP_VERSION 98)
if ("cxx_std_11" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	set(CPP_VERSION 11)
endif()
if ("cxx_std_14" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	set(CPP_VERSION 14)
endif()
if ("cxx_std_17" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	set(CPP_VERSION 17)
endif()
if ("cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	set(CPP_VERSION 20)
endif()
if ("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
	set(CPP_VERSION 23)
endif()

message("Your compiler supports : c++${CPP_VERSION}")
set(CMAKE_CXX_STANDARD ${CPP_VERSION})

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_DEBUG "${DCMAKE_CXX_FLAGS_DEBUG} -g -O0")

message("CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SHARED_DEFINES ${SHARED_DEFINES} _DEBUG)
else()
    set(SHARED_DEFINES ${SHARED_DEFINES} NDEBUG)
endif()

message ("SHARED_DEFINES = ${SHARED_DEFINES}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output/${CMAKE_BUILD_TYPE})

set(COMPILE_OPTIONS -Werror)

message("loading packages...")

find_package(benchmark REQUIRED)

add_executable(benchmark
    benchmark.cpp
)

target_link_libraries(benchmark benchmark::benchmark)

target_compile_definitions(benchmark PRIVATE ${SHARED_DEFINES})

target_compile_options(benchmark PRIVATE ${COMPILE_OPTIONS})
