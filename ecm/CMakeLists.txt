add_library(ecm STATIC
    src/Ecm.cpp
    src/Heightfield.cpp
    src/Contours.cpp
    src/MedialAxis.cpp)
add_library(ecm::ecm ALIAS ecm)
target_include_directories(ecm PUBLIC include)
target_include_directories(ecm SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/boost_voronoi/include")
if(MSVC)
    target_compile_options(ecm PRIVATE /W4 /utf-8)
else()
    target_compile_options(ecm PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(ecm_tests tests/ecm_tests.cpp)
    target_link_libraries(ecm_tests PRIVATE ecm::ecm)
    add_test(NAME ecm COMMAND ecm_tests)
endif()
