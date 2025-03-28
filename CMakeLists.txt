cmake_minimum_required(VERSION 3.20)
project(PIMCompiler VERSION 1.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

# Include Directories
include_directories(
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

# Compiler Definitions
add_definitions(${LLVM_DEFINITIONS})

# Compiler Executable
add_executable(pim_compiler 
    src/main.cpp
)

# Link Libraries
target_link_libraries(pim_compiler 
    PRIVATE
    ${LLVM_LIBRARIES}
)