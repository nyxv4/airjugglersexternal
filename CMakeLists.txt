cmake_minimum_required(VERSION 3.20)
project(NyxExternal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NyxExternal WIN32
    src/main.cpp
    src/core/memory.cpp
    src/core/http.cpp
    src/game/offsets.cpp
    src/game/sdk.cpp
    src/menu/settings.cpp
    src/render/overlay.cpp
    ext/imgui/imgui.cpp
    ext/imgui/imgui_draw.cpp
    ext/imgui/imgui_tables.cpp
    ext/imgui/imgui_widgets.cpp
    ext/imgui/backends/imgui_impl_win32.cpp
    ext/imgui/backends/imgui_impl_dx11.cpp
)

target_include_directories(NyxExternal PRIVATE
    include
    ext/imgui
    ext/imgui/backends
)

target_link_libraries(NyxExternal PRIVATE
    d3d11 dxgi dwmapi wininet psapi
)

target_compile_definitions(NyxExternal PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
