cmake_minimum_required(VERSION 3.25)   # 3.25: FetchContent SYSTEM e workflow preset
project(i8086emu VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20, non gnu++20
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Tipo di build" FORCE)
endif()

option(EMU_BUILD_TESTS "Compila i test"               ON)
option(EMU_SANITIZERS  "ASan/UBSan nelle build Debug" ON)
option(EMU_WERROR      "Warning trattati come errori" OFF)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(CompilerOptions)

add_subdirectory(src/core)
add_subdirectory(src/debugger)
add_subdirectory(src/cli)

if(EMU_BUILD_TESTS)
  include(Dependencies)
  enable_testing()
  add_subdirectory(tests)
endif()
