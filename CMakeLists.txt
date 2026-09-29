cmake_minimum_required(VERSION 3.16)
project(genrecomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(GENRECOMP_BUILD_EXAMPLES "Build example programs" ON)
option(GENRECOMP_BUILD_DEBUG_RUNNER "Build debug runner with GDB stub" OFF)

# SDL2
find_package(SDL2 REQUIRED)

# --- Genesis Plus GX hardware backend (static lib) ---
set(GPGX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/Genesis-Plus-GX/core)

add_library(genplusgx_hw STATIC
    # Core
    ${GPGX_DIR}/genesis.c
    ${GPGX_DIR}/system.c
    ${GPGX_DIR}/io_ctrl.c
    ${GPGX_DIR}/loadrom.c
    ${GPGX_DIR}/mem68k.c
    ${GPGX_DIR}/membnk.c
    ${GPGX_DIR}/memz80.c
    ${GPGX_DIR}/state.c
    ${GPGX_DIR}/vdp_ctrl.c
    ${GPGX_DIR}/vdp_render.c

    # M68K CPU (Musashi — needed for memory map infrastructure)
    ${GPGX_DIR}/m68k/m68kcpu.c
    ${GPGX_DIR}/m68k/s68kcpu.c

    # Z80 CPU
    ${GPGX_DIR}/z80/z80.c

    # Sound
    ${GPGX_DIR}/sound/blip_buf.c
    ${GPGX_DIR}/sound/eq.c
    ${GPGX_DIR}/sound/psg.c
    ${GPGX_DIR}/sound/sound.c
    ${GPGX_DIR}/sound/ym2612.c
    ${GPGX_DIR}/sound/ym2413.c
    ${GPGX_DIR}/sound/ym3438.c
    ${GPGX_DIR}/sound/opll.c

    # Cartridge hardware
    ${GPGX_DIR}/cart_hw/md_cart.c
    ${GPGX_DIR}/cart_hw/sms_cart.c
    ${GPGX_DIR}/cart_hw/sram.c
    ${GPGX_DIR}/cart_hw/eeprom_93c.c
    ${GPGX_DIR}/cart_hw/eeprom_i2c.c
    ${GPGX_DIR}/cart_hw/eeprom_spi.c
    ${GPGX_DIR}/cart_hw/flash_cfi.c
    ${GPGX_DIR}/cart_hw/ggenie.c
    ${GPGX_DIR}/cart_hw/areplay.c
    ${GPGX_DIR}/cart_hw/megasd.c
    ${GPGX_DIR}/cart_hw/yx5200.c
    ${GPGX_DIR}/cart_hw/svp/svp.c
    ${GPGX_DIR}/cart_hw/svp/ssp16.c

    # Input hardware
    ${GPGX_DIR}/input_hw/input.c
    ${GPGX_DIR}/input_hw/gamepad.c
    ${GPGX_DIR}/input_hw/activator.c
    ${GPGX_DIR}/input_hw/graphic_board.c
    ${GPGX_DIR}/input_hw/lightgun.c
    ${GPGX_DIR}/input_hw/mouse.c
    ${GPGX_DIR}/input_hw/paddle.c
    ${GPGX_DIR}/input_hw/smash.c
    ${GPGX_DIR}/input_hw/sportspad.c
    ${GPGX_DIR}/input_hw/teamplayer.c
    ${GPGX_DIR}/input_hw/terebi_oekaki.c
    ${GPGX_DIR}/input_hw/xe_1ap.c

    # Mega CD stubs (required by genesis.h include chain)
    ${GPGX_DIR}/cd_hw/cdc.c
    ${GPGX_DIR}/cd_hw/cdd.c
    ${GPGX_DIR}/cd_hw/gfx.c
    ${GPGX_DIR}/cd_hw/pcm.c
    ${GPGX_DIR}/cd_hw/scd.c
    ${GPGX_DIR}/cd_hw/cd_cart.c

    # Our OSD implementation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/osd.c
)

target_include_directories(genplusgx_hw
    PUBLIC  ${GPGX_DIR}
    PUBLIC  ${GPGX_DIR}/m68k
    PUBLIC  ${GPGX_DIR}/z80
    PUBLIC  ${GPGX_DIR}/sound
    PUBLIC  ${GPGX_DIR}/cart_hw
    PUBLIC  ${GPGX_DIR}/cart_hw/svp
    PUBLIC  ${GPGX_DIR}/cd_hw
    PUBLIC  ${GPGX_DIR}/input_hw
    PUBLIC  ${GPGX_DIR}/ntsc
    PUBLIC  ${GPGX_DIR}/sound/minimp3
    # Our osd.h lives in src/
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
    # genrecomp public headers (for osd.c -> input.h)
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(genplusgx_hw PRIVATE
    USE_32BPP_RENDERING
    LSB_FIRST
    HAVE_NO_SPRITE_LIMIT
)

# Suppress warnings in third-party code and fix MSVC compatibility
if(MSVC)
    target_compile_options(genplusgx_hw PRIVATE /W0)
    # MSVC doesn't support __inline__ (GCC keyword) — remap to __inline
    target_compile_definitions(genplusgx_hw PRIVATE __inline__=__inline)
else()
    target_compile_options(genplusgx_hw PRIVATE -w)
endif()

# --- genrecomp adapter library (static lib) ---
add_library(genrecomp STATIC
    src/genrecomp.c
    src/m68k.c
    src/bus.c
    src/func_table.c
    src/vdp.c
    src/ym2612.c
    src/psg.c
    src/z80.c
    src/io.c
    src/platform_sdl.c
    src/input.c
    src/debug.c
)
target_include_directories(genrecomp
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${GPGX_DIR}
    PRIVATE ${GPGX_DIR}/m68k
    PRIVATE ${GPGX_DIR}/z80
    PRIVATE ${GPGX_DIR}/sound
    PRIVATE ${GPGX_DIR}/cart_hw
    PRIVATE ${GPGX_DIR}/cart_hw/svp
    PRIVATE ${GPGX_DIR}/cd_hw
    PRIVATE ${GPGX_DIR}/input_hw
    PRIVATE ${GPGX_DIR}/ntsc
)
target_compile_definitions(genrecomp PRIVATE
    USE_32BPP_RENDERING
    LSB_FIRST
    HAVE_NO_SPRITE_LIMIT
)
target_link_libraries(genrecomp
    PUBLIC  genplusgx_hw
    PUBLIC  SDL2::SDL2
)

# --- Example: minimal recomp ---
if(GENRECOMP_BUILD_EXAMPLES)
    add_executable(genrecomp_minimal examples/minimal/main.c)
    target_link_libraries(genrecomp_minimal PRIVATE genrecomp SDL2::SDL2main)
endif()

# --- Runtime self-check (ctest) ---
enable_testing()
add_executable(genrecomp_test_runtime tests/test_runtime.c)
target_link_libraries(genrecomp_test_runtime PRIVATE genrecomp)
add_test(NAME runtime COMMAND genrecomp_test_runtime)

# --- Reference runner (GPGX interpreter, ground truth for diffing) ---
add_executable(genrecomp_ref tools/ref_runner/main.c)
target_link_libraries(genrecomp_ref PRIVATE genrecomp SDL2::SDL2main)
target_include_directories(genrecomp_ref PRIVATE ${GPGX_DIR} ${GPGX_DIR}/m68k ${GPGX_DIR}/z80
    ${GPGX_DIR}/sound ${GPGX_DIR}/cart_hw ${GPGX_DIR}/cart_hw/svp ${GPGX_DIR}/cd_hw
    ${GPGX_DIR}/input_hw ${GPGX_DIR}/ntsc ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(genrecomp_ref PRIVATE USE_32BPP_RENDERING LSB_FIRST HAVE_NO_SPRITE_LIMIT)

# --- Debug runner ---
if(GENRECOMP_BUILD_DEBUG_RUNNER)
    add_executable(genrecomp_debug_runner tools/debug_runner/main.c)
    target_link_libraries(genrecomp_debug_runner PRIVATE genrecomp SDL2::SDL2main)
    target_compile_definitions(genrecomp_debug_runner PRIVATE GENRECOMP_DEBUG=1)
endif()
