# recomp-ui: a static library, brought in by the host with add_subdirectory
# (README.md). The host makes the dependencies' targets and may name them
# before add_subdirectory:
#
#   RUI_IMGUI_TARGET  Dear ImGui 1.92+ with its SDL3 platform backend
#                     (backends/imgui_impl_sdl3.cpp) compiled in; the
#                     include path has imgui.h and backends/. Default: imgui
#   RUI_SDL_TARGET    SDL3. Default: SDL3::SDL3
#   RUI_PLUME_TARGET  plume (D3D12 and Vulkan). Default: plume
#   RUI_ICON_FONT     an icon font (TTF, codepoints in U+E000-U+F8FF)
#                     merged into the text faces in place of the embedded
#                     Material Symbols subset (fonts/README.md)
#
# Shaders are compiled by the host (rui::InitInfo::compileShader), so no
# shader compiler is linked here.
cmake_minimum_required(VERSION 3.20)
project(recomp_ui LANGUAGES CXX)

if(NOT DEFINED RUI_IMGUI_TARGET)
    set(RUI_IMGUI_TARGET imgui)
endif()
if(NOT DEFINED RUI_SDL_TARGET)
    set(RUI_SDL_TARGET SDL3::SDL3)
endif()
if(NOT DEFINED RUI_PLUME_TARGET)
    set(RUI_PLUME_TARGET plume)
endif()
set(RUI_FONTS ${CMAKE_CURRENT_SOURCE_DIR}/fonts)
if(NOT DEFINED RUI_ICON_FONT)
    set(RUI_ICON_FONT ${RUI_FONTS}/MaterialSymbolsRounded-Subset.ttf)
endif()

# The fonts, as byte arrays (src/internal.h, rui::fonts).
set(RUI_FONTS_CPP ${CMAKE_CURRENT_BINARY_DIR}/rui_fonts.cpp)
set(RUI_FONT_FILES
    SANS_REGULAR=${RUI_FONTS}/IBMPlexSans-Regular.ttf
    SANS_SEMIBOLD=${RUI_FONTS}/IBMPlexSans-SemiBold.ttf
    MONO_MEDIUM=${RUI_FONTS}/IBMPlexMono-Medium.ttf
    ICONS=${RUI_ICON_FONT})
add_custom_command(
    OUTPUT ${RUI_FONTS_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${RUI_FONTS_CPP} -DNAMESPACE=rui::fonts "-DFILES=${RUI_FONT_FILES}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_files.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_files.cmake ${RUI_FONTS}/IBMPlexSans-Regular.ttf ${RUI_FONTS}/IBMPlexSans-SemiBold.ttf
        ${RUI_FONTS}/IBMPlexMono-Medium.ttf ${RUI_ICON_FONT}
    COMMENT "Embedding the overlay's fonts"
    VERBATIM)

add_library(recomp_ui STATIC
    src/gate.cpp
    src/imgui_impl_plume.cpp
    src/panel.cpp
    src/rui.cpp
    src/settings.cpp
    src/stats.cpp
    src/style.cpp
    src/toasts.cpp
    src/widgets.cpp
    ${RUI_FONTS_CPP})
add_library(recomp_ui::recomp_ui ALIAS recomp_ui)
target_compile_features(recomp_ui PUBLIC cxx_std_20)
target_include_directories(recomp_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(recomp_ui PUBLIC ${RUI_IMGUI_TARGET} ${RUI_SDL_TARGET} ${RUI_PLUME_TARGET})
# rui/rui.h includes plume's header, which brings in windows.h on Windows:
# NOMINMAX keeps its min and max macros away from std::min and std::max.
if(WIN32)
    target_compile_definitions(recomp_ui PUBLIC NOMINMAX PRIVATE WIN32_LEAN_AND_MEAN)
endif()
