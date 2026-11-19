cmake_minimum_required(VERSION 3.20)
project(crowdSim VERSION 0.1.0 LANGUAGES C CXX)

# OFF builds only the portable libraries and their tests, on any platform and without the SDKs.
option(CROWDSIM_BUILD_VIEWER "Build the ovstage adapter and the RTX viewer (Windows x64, run setup_deps.bat first)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CROWDSIM_BUILD_VIEWER)
    # The pinned viewer SDK loaders use the release CRT in Debug and Release. Set before any
    # target exists, so the libraries linked into the viewer match.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()
include(CTest)

# Explicit corridor map; independent of USD and the viewer.
set(ECM_BUILD_TESTS ${BUILD_TESTING})
add_subdirectory(ecm)

if(NOT CROWDSIM_BUILD_VIEWER)
    return()
endif()
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The viewer uses the Windows x64 Omniverse SDKs and MSVC. Run build.bat, or configure "
                        "with -DCROWDSIM_BUILD_VIEWER=OFF to build only the libraries.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The viewer requires a 64-bit build.")
endif()

foreach(sdk ovstage ovrtx)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/deps/${sdk}/lib/cmake/${sdk}/${sdk}Config.cmake")
        message(FATAL_ERROR "Missing ${sdk}. Run setup_deps.bat before building.")
    endif()
    find_package(${sdk} CONFIG REQUIRED PATHS "${PROJECT_SOURCE_DIR}/deps/${sdk}/lib/cmake/${sdk}" NO_DEFAULT_PATH)
endforeach()

# ovstage adapter: stage triangles in, overlay layers out. Uses the ovstage found above.
add_subdirectory(ecm_ovstage)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)
find_package(OpenGL REQUIRED)

add_library(viewer_ui STATIC
    src/viewer/viewer_imgui_theme.cpp
    src/viewer/viewer_imgui_widgets.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_impl_glfw.cpp)
target_include_directories(viewer_ui PUBLIC third_party/imgui src/viewer)
target_link_libraries(viewer_ui PUBLIC glfw OpenGL::GL)

add_library(viewer_core STATIC
    src/viewer/Camera.cpp
    src/viewer/StageCamera.cpp
    src/viewer/RtxView.cpp)
target_include_directories(viewer_core PUBLIC src/viewer)
target_link_libraries(viewer_core PUBLIC ecm_ovstage::ecm_ovstage ovstage::ovstage_static ovrtx::ovrtx_static
    shell32 ole32)
target_compile_options(viewer_core PRIVATE /W4 /utf-8)
# Runtime paths come from this build's SDK configuration, not a neighboring checkout.
target_compile_definitions(viewer_core PRIVATE
    CROWDSIM_OVSTAGE_BIN="${PROJECT_SOURCE_DIR}/deps/ovstage/bin"
    CROWDSIM_OVRTX_BIN="${PROJECT_SOURCE_DIR}/deps/ovrtx/bin")

add_executable(crowdSim_viewer src/viewer/main.cpp src/viewer/FileDialog.cpp)
target_link_libraries(crowdSim_viewer PRIVATE viewer_core viewer_ui)
target_compile_options(crowdSim_viewer PRIVATE /W4 /utf-8)
add_custom_command(TARGET crowdSim_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:crowdSim_viewer>/resources"
    COMMENT "Copy viewer fonts")
