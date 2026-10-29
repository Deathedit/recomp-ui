# The framework on its own: a host of its own making (Dear ImGui, SDL3 and
# plume from source folders) and a self-test that needs no GPU or window.
#
#   cmake -S test -B build -G Ninja -DRUI_TEST_IMGUI_DIR=<imgui> -DRUI_TEST_SDL_DIR=<SDL> -DRUI_TEST_PLUME_DIR=<plume>
#   ninja -C build && build/rui_selftest
cmake_minimum_required(VERSION 3.20)
project(recomp_ui_selftest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RUI_TEST_IMGUI_DIR "" CACHE PATH "Dear ImGui's source folder (1.92+)")
set(RUI_TEST_SDL_DIR "" CACHE PATH "SDL3's source folder")
set(RUI_TEST_PLUME_DIR "" CACHE PATH "plume's source folder")
foreach(dir RUI_TEST_IMGUI_DIR RUI_TEST_SDL_DIR RUI_TEST_PLUME_DIR)
    if(NOT EXISTS "${${dir}}")
        message(FATAL_ERROR "Set -D${dir}=<folder>")
    endif()
endforeach()

set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${RUI_TEST_SDL_DIR} sdl EXCLUDE_FROM_ALL)
add_subdirectory(${RUI_TEST_PLUME_DIR} plume EXCLUDE_FROM_ALL)

add_library(imgui STATIC
    ${RUI_TEST_IMGUI_DIR}/imgui.cpp
    ${RUI_TEST_IMGUI_DIR}/imgui_draw.cpp
    ${RUI_TEST_IMGUI_DIR}/imgui_tables.cpp
    ${RUI_TEST_IMGUI_DIR}/imgui_widgets.cpp
    ${RUI_TEST_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp)
target_include_directories(imgui PUBLIC ${RUI_TEST_IMGUI_DIR})
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

set(RUI_SDL_TARGET SDL3::SDL3-static)
add_subdirectory(.. recomp_ui)

add_executable(rui_selftest selftest.cpp)
target_link_libraries(rui_selftest PRIVATE recomp_ui)
enable_testing()
add_test(NAME rui_selftest COMMAND rui_selftest ${CMAKE_CURRENT_BINARY_DIR})
