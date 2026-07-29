cmake_minimum_required(VERSION 3.16)

project(dlnaplay)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
find_package(RmlUi REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(FREETYPE2 REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
                         libavcodec libavformat libavutil libswresample libswscale)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
pkg_check_modules(TINYXML2 REQUIRED IMPORTED_TARGET tinyxml2)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET luajit)

add_executable(dlnaplay
    src/main.cpp
    src/app.cpp
    src/app_browse.cpp
    src/app_watch.cpp
    src/player.cpp
    src/visualizer.cpp
    src/browse/dlna_source.cpp
    src/browse/fs_source.cpp
    src/browse/lua_source.cpp
    src/upnp/http.cpp
    src/upnp/ssdp.cpp
    src/upnp/dlna.cpp
    src/RmlUi/Backend_SDL_Renderer.cpp
    src/RmlUi/Renderer_SDL.cpp
    src/RmlUi/Platform_SDL.cpp
)

target_include_directories(dlnaplay PRIVATE src)

target_compile_options(dlnaplay PRIVATE -Wall -Werror)

target_link_libraries(dlnaplay PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    RmlUi::Core
    PkgConfig::FREETYPE2
    PkgConfig::FFMPEG
    PkgConfig::SDL2_IMAGE
    PkgConfig::TINYXML2
    PkgConfig::LUA
    Threads::Threads
)
