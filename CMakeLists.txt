cmake_minimum_required(VERSION 3.0)

project(Kaleidoscope)

# Tested only on Clang
if(NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "AppleClang|Clang")
  message(FATAL_ERROR "Unsupported compiler: " ${CMAKE_CXX_COMPILER_ID})
endif()

# Debug by default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
  message(STATUS "Setting build type to '${CMAKE_BUILD_TYPE}' as none was specified.")
endif()

# Executable
set(executable_name ${PROJECT_NAME}-${CMAKE_BUILD_TYPE})

file(GLOB_RECURSE sources "src/*.cpp")
add_executable(${executable_name} ${sources} "src/main.cpp")

# Includes
file(GLOB_RECURSE headers "src/*.h")
set(llvm_include_path "/usr/local/Cellar/llvm/8.0.0/include/")
target_include_directories(${executable_name} PRIVATE ${headers} PRIVATE ${llvm_include_path})

set(llvm_path "/usr/local/Cellar/llvm/8.0.0/lib/libLLVM.dylib")
target_link_libraries(${executable_name} ${llvm_path})

# Flags
list(APPEND cflags -arch x86_64 --std=c++17)
list(APPEND cflags -Wall -Wextra -Wno-unused-parameter -Wno-newline-eof)

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
  message(STATUS "Adding debug flags")
  list(APPEND cflags -g -O0)
endif()

target_compile_options(${executable_name} PRIVATE ${cflags})
