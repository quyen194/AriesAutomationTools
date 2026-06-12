cmake_minimum_required(VERSION 3.25)
project(AriesAutomationTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

# Force UTF-8 source/execution charset on MSVC (fixes codepage 932 issues)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ── Dependencies (git submodules in third_party/) ─────────────────────────────
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL2)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/nlohmann_json)

# ImGui has no CMakeLists — compile its sources directly (same as before)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

# ── Platform sources ──────────────────────────────────────────────────────────
if(WIN32)
    set(PLATFORM_SOURCES
        src/input/windows/win_input.cpp
        src/monitor/windows/win_monitor.cpp
        src/window/windows/win_window.cpp
        src/hotkey/windows/win_hotkey.cpp
        src/ui/tray_manager_win.cpp
        src/single_instance_win.cpp
    )
elseif(APPLE)
    set(PLATFORM_SOURCES
        src/input/macos/macos_input.cpp
        src/monitor/macos/macos_monitor.cpp
        src/window/macos/macos_window.cpp
        src/hotkey/macos/macos_hotkey.cpp
        src/ui/tray_manager_stub.cpp
        src/single_instance_unix.cpp
    )
else()
    set(PLATFORM_SOURCES
        src/input/linux/linux_input.cpp
        src/monitor/linux/linux_monitor.cpp
        src/window/linux/linux_window.cpp
        src/hotkey/linux/linux_hotkey.cpp
        src/ui/tray_manager_stub.cpp
        src/single_instance_unix.cpp
    )
endif()

# ── ImGui sources ─────────────────────────────────────────────────────────────
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# ── Windows resource file (EXE icon + version info) ──────────────────────────
if(WIN32)
    set(WIN_RESOURCES assets/resources.rc)
    # Allow resources.rc to #include "version.h" from src/
    set_source_files_properties(assets/resources.rc PROPERTIES
        COMPILE_FLAGS "/I\"${CMAKE_SOURCE_DIR}/src\""
    )
else()
    set(WIN_RESOURCES "")
endif()

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(AriesAutomationTools
    src/main.cpp
    src/core/engine.cpp
    src/core/scheduler.cpp
    src/core/record_engine.cpp
    src/core/trigger_manager.cpp
    src/input/key_map.cpp
    src/config/config_manager.cpp
    src/ui/app_ui.cpp
    src/ui/widgets/workflow_list.cpp
    src/ui/widgets/activity_editor.cpp
    src/ui/widgets/window_picker.cpp
    src/ui/widgets/record_overlay.cpp
    ${PLATFORM_SOURCES}
    ${IMGUI_SOURCES}
    ${WIN_RESOURCES}
)

if(WIN32)
    set_target_properties(AriesAutomationTools PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(AriesAutomationTools PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(AriesAutomationTools PRIVATE
    SDL2::SDL2-static
    SDL2::SDL2main
    nlohmann_json::nlohmann_json
)

if(APPLE)
    target_link_libraries(AriesAutomationTools PRIVATE
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework Carbon"
        "-framework IOKit"
        "-framework AppKit"
    )
elseif(UNIX)
    find_package(X11 REQUIRED)
    target_link_libraries(AriesAutomationTools PRIVATE X11::X11 X11::Xss Xtst)
endif()

# Copy config.json next to the exe on build
add_custom_command(TARGET AriesAutomationTools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/config.json
        $<TARGET_FILE_DIR:AriesAutomationTools>/config.json
)
