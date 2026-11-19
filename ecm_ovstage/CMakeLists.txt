# Adapter between ovstage USD stages and the ecm library: scene triangles in, overlay layers out.
# It reads and writes the stage only at ordinals the host passes in. The host owns the stage,
# seals ordinals, computes the hierarchy and drives its renderer; this library never uses ovrtx.
#
# The host provides ovstage, so the adapter and the host link the same SDK: find_package(ovstage)
# before adding this directory, or set ovstage_DIR. ECM_OVSTAGE_TARGET picks the target to link.
cmake_minimum_required(VERSION 3.20)
project(ecm_ovstage VERSION 0.1.0 LANGUAGES CXX)

if(NOT TARGET ecm::ecm)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ecm" "${CMAKE_CURRENT_BINARY_DIR}/ecm")
endif()
set(ECM_OVSTAGE_TARGET "ovstage::ovstage_static" CACHE STRING
    "ovstage target the adapter links; use the one the host links")
if(NOT TARGET ${ECM_OVSTAGE_TARGET})
    find_package(ovstage CONFIG REQUIRED)
endif()

add_library(ecm_ovstage STATIC
    src/StageData.cpp
    src/StageMesh.cpp
    src/EcmOverlay.cpp)
add_library(ecm_ovstage::ecm_ovstage ALIAS ecm_ovstage)
target_compile_features(ecm_ovstage PUBLIC cxx_std_17)
target_include_directories(ecm_ovstage PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ecm_ovstage PUBLIC ecm::ecm ${ECM_OVSTAGE_TARGET})
if(MSVC)
    target_compile_options(ecm_ovstage PRIVATE /W4 /utf-8)
else()
    target_compile_options(ecm_ovstage PRIVATE -Wall -Wextra -Wpedantic)
endif()
