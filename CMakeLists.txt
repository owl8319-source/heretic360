# Heretic 360: Crispy Doom 7.2's Heretic for the Xbox 360, built with xsdk and SDL-xenon (README.md).
#
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$HOME/xsdk/cmake/xsdk.cmake \
#     -DSDL3_DIR=$HOME/SDL-xenon/build-xsdk -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# That makes build/heretic.xex.
cmake_minimum_required(VERSION 3.25)
project(heretic360 C)

find_package(SDL3 CONFIG REQUIRED)

# Crispy Doom's sources, as released but for a few lines (README.md, "What changed in Crispy Doom's code").
file(GLOB CRISPY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB HERETIC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/heretic/*.c")
# d_main.c includes heretic_icon.c itself.
list(FILTER HERETIC_SOURCES EXCLUDE REGEX "heretic_icon\\.c$")
# Crispy Doom's OPL library: the music player's chip (Nuked OPL3) and its timing; opl_sdl.c's place is taken by
# xenon/opl_xsdk.c.
file(GLOB OPL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/opl/*.c")
# The Xbox 360's side: video, controller, sound, timer, system, and stubs for what's left out.
file(GLOB PORT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/xenon/*.c")

add_executable(heretic ${PORT_SOURCES} ${CRISPY_SOURCES} ${HERETIC_SOURCES} ${OPL_SOURCES})
set_target_properties(heretic PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)
# xenon/ first: its headers replace Crispy Doom's SDL2 ones (i_swap.h, i_input.h, i_joystick.h, the textscreen
# headers) and give config.h.
target_include_directories(heretic PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/xenon"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/opl")
target_compile_definitions(heretic PRIVATE HERETIC360)
# Crispy Doom's own code builds with warnings on PowerPC; they're shown, not fatal.
target_compile_options(heretic PRIVATE -Wall -Wno-unused-parameter -fno-strict-aliasing)
target_link_libraries(heretic PRIVATE SDL3::SDL3-static)

# Writes heretic_shaders.h, with blit_vert and blit_frag.
xsdk_add_shaders(heretic xenon/blit.vert xenon/blit.frag)
xsdk_add_xex(heretic NAME "Heretic" VERSION 0.8.0.0)
