cmake_minimum_required(VERSION 3.19)
project(
  JUVIA
  VERSION 0.1.0
  DESCRIPTION "Juvia"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_DEV_TOOL "Build development tools" OFF)

include(BuildType)
include(CheckLanguage)

# nvcc 12.x does not support all GCC 14 built-ins used in C++17 type_traits.
# Pass -ccbin explicitly so nvcc uses the system GCC 13 as its host compiler,
# regardless of which g++ CMake resolved from PATH (conda often provides GCC
# 14).
set(CMAKE_CUDA_ARCHITECTURES
    "80;89;120"
    CACHE STRING "CUDA architectures to build for")
if(EXISTS "/usr/bin/g++-13")
  set(CMAKE_CUDA_HOST_COMPILER
      "/usr/bin/g++-13"
      CACHE FILEPATH "Host compiler for CUDA (nvcc)")
endif()
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
else()
  message(FATAL_ERROR "No CUDA compiler found")
endif()

include(CCache)

find_package(OpenMP REQUIRED)

# Prebuilt juvia engine vendored at the submission root: headers under include/,
# libjuvia.so under lib/, CMake package config under lib/cmake/juvia/. The juvia
# package prefix is therefore this directory itself.
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
find_package(juvia REQUIRED)

include(CPM)
cpmaddpackage(
  GITHUB_REPOSITORY
  nlohmann/json
  VERSION
  3.11.2
  OPTIONS
  EXCLUDE_FROM_ALL
  YES)

add_subdirectory("export")
