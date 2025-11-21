cmake_minimum_required(VERSION 3.15)
project(silikangaroo VERSION 0.1.0 LANGUAGES CXX C OBJCXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# M1 Optimization flags
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    add_compile_options(-mcpu=apple-m1)

    # OpenMP on macOS with Homebrew
    if(EXISTS "/opt/homebrew/opt/libomp")
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
        set(OpenMP_C_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "/opt/homebrew/opt/libomp/lib/libomp.dylib")

        include_directories("/opt/homebrew/opt/libomp/include")
        link_directories("/opt/homebrew/opt/libomp/lib")
    endif()
endif()

# Dependencies
find_package(OpenMP REQUIRED)

# Metal Frameworks
find_library(METAL_LIBRARY Metal)
find_library(FOUNDATION_LIBRARY Foundation)
find_library(QUARTZCORE_LIBRARY QuartzCore)

# Find GMP
find_path(GMP_INCLUDE_DIR NAMES gmp.h)
find_path(GMPXX_INCLUDE_DIR NAMES gmpxx.h)
find_library(GMP_LIBRARY NAMES gmp)
find_library(GMPXX_LIBRARY NAMES gmpxx)

if(GMP_INCLUDE_DIR AND GMP_LIBRARY AND GMPXX_INCLUDE_DIR AND GMPXX_LIBRARY)
    message(STATUS "Found GMP: ${GMP_LIBRARY}")
    message(STATUS "Found GMPXX: ${GMPXX_LIBRARY}")
    include_directories(${GMP_INCLUDE_DIR} ${GMPXX_INCLUDE_DIR})
else()
    message(FATAL_ERROR "GMP or GMPXX not found. Please install it (e.g. brew install gmp)")
endif()

# Fetch libsecp256k1
message(STATUS "Fetching libsecp256k1...")
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "Enable recovery module")
set(SECP256K1_ENABLE_EXPERIMENTAL ON CACHE BOOL "Enable experimental modules")
set(SECP256K1_ENABLE_MODULE_ECDH ON CACHE BOOL "Enable ECDH module")

FetchContent_Declare(
    libsecp256k1
    GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(libsecp256k1)

include_directories(include)

# Compile Metal Shaders
set(METAL_SRC "src/kernels.metal")
set(METAL_LIB "${CMAKE_BINARY_DIR}/default.metallib")

# Custom command to compile Metal shaders
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/default.metallib
    COMMAND xcrun -sdk macosx metal -c ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels.metal -o ${CMAKE_CURRENT_BINARY_DIR}/kernels.air
    COMMAND xcrun -sdk macosx metallib ${CMAKE_CURRENT_BINARY_DIR}/kernels.air -o ${CMAKE_CURRENT_BINARY_DIR}/default.metallib
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels.metal
    COMMENT "Compiling Metal kernels"
)

add_custom_target(MetalKernels DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/default.metallib)

# Sources
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.mm")

add_executable(silikangaroo ${SOURCES})
add_dependencies(silikangaroo MetalKernels)

target_link_libraries(silikangaroo
    PRIVATE
    OpenMP::OpenMP_CXX
    ${GMP_LIBRARY}
    ${GMPXX_LIBRARY}
    secp256k1
    ${METAL_LIBRARY}
    ${FOUNDATION_LIBRARY}
    ${QUARTZCORE_LIBRARY}
)

# Tool: gen_key
add_executable(gen_key tools/gen_key.cpp src/ECC.cpp src/Utils.cpp)
target_link_libraries(gen_key
    PRIVATE
    ${GMP_LIBRARY}
    ${GMPXX_LIBRARY}
    secp256k1
)
target_include_directories(gen_key PRIVATE include)

# Warning level
if(MSVC)
    target_compile_options(silikangaroo PRIVATE /W4)
else()
    target_compile_options(silikangaroo PRIVATE -Wall -Wextra -Wpedantic)
endif()
