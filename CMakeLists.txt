cmake_minimum_required(VERSION 3.25)

project(125A_Arporator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ArporatorCore STATIC
    source/arporator_engine.cpp
    source/arporator_engine.h
)

target_include_directories(ArporatorCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_compile_features(ArporatorCore PUBLIC cxx_std_17)

add_executable(ArporatorEngineTests tests/arporator_engine_tests.cpp)
target_link_libraries(ArporatorEngineTests PRIVATE ArporatorCore)

enable_testing()
add_test(NAME ArporatorEngineTests COMMAND ArporatorEngineTests)
