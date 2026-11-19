# Explicit corridor map library. Needs only a C++17 compiler; the Voronoi headers it uses are
# vendored in third_party/. Build it on its own (cmake -S ecm) or add it to another project
# with add_subdirectory and link ecm::ecm.
cmake_minimum_required(VERSION 3.20)
project(ecm VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ECM_TOP_LEVEL ON)
else()
    set(ECM_TOP_LEVEL OFF)
endif()
option(ECM_BUILD_TESTS "Build the ECM unit tests" ${ECM_TOP_LEVEL})

add_library(ecm STATIC
    src/Ecm.cpp
    src/Heightfield.cpp
    src/Contours.cpp
    src/MedialAxis.cpp)
add_library(ecm::ecm ALIAS ecm)
target_compile_features(ecm PUBLIC cxx_std_17)
target_include_directories(ecm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(ecm SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost_voronoi/include")
if(MSVC)
    target_compile_options(ecm PRIVATE /W4 /utf-8)
else()
    target_compile_options(ecm PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ECM_BUILD_TESTS)
    enable_testing()
    add_executable(ecm_tests tests/ecm_tests.cpp)
    target_link_libraries(ecm_tests PRIVATE ecm::ecm)
    add_test(NAME ecm COMMAND ecm_tests)
endif()
