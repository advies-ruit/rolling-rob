cmake_minimum_required(VERSION 3.16)
project(rolling_rob C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# SDL2: the web (Emscripten), macOS, Windows and Linux.

# The game's logical resolution, 480x272; the window or page scales it.
# Overridable; keep width/height and
# ROLLING_ROB_VIEW_SCALE in proportion or the visible slice of the world - and
# with it the difficulty - changes. 480x272 @ 0.25 and 720x408 @ 0.375 both
# show exactly the original 1920x1080 canvas.
if(NOT ROLLING_ROB_SCREEN_WIDTH)
    set(ROLLING_ROB_SCREEN_WIDTH 480)
endif()
if(NOT ROLLING_ROB_SCREEN_HEIGHT)
    set(ROLLING_ROB_SCREEN_HEIGHT 272)
endif()
if(NOT ROLLING_ROB_SIM_ZOOM)
    set(ROLLING_ROB_SIM_ZOOM 2)
endif()
# World-to-screen scale. 0.25 shows exactly what the original 1920x1080
# canvas shows (1:4). Anything up to ~0.35 also works with the original
# camera; larger values zoom in on Rob at the cost of look-ahead.
if(NOT ROLLING_ROB_VIEW_SCALE)
    set(ROLLING_ROB_VIEW_SCALE 0.25)
endif()

# ThorVG (vector graphics, C++) is not used: LV_USE_THORVG=0 in lv_conf.h.
# Keeping it out of the build also lets a C-only cross toolchain build LVGL.
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "ThorVG is not used by Rolling Rob" FORCE)
add_subdirectory(third_party/lvgl)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The remaining .cpp files in LVGL (VG-Lite/ThorVG glue) are empty with this
# lv_conf.h; drop them so no C++ compiler is needed at all.
get_target_property(RR_LVGL_SOURCES lvgl SOURCES)
list(FILTER RR_LVGL_SOURCES EXCLUDE REGEX "\\.cpp$")
set_target_properties(lvgl PROPERTIES SOURCES "${RR_LVGL_SOURCES}")

add_library(rolling_rob_game STATIC
    src/game/game.c
    src/game/draw.c
    src/game/art.c
    src/game/gfx.c
    src/game/level.c
    src/game/terrain.c
    src/game/entities.c
    src/game/hud.c
    src/game/ui.c
    src/game/fps_overlay.c
    src/game/draw_mt.c
    src/game/prof.c
    src/game/level_thumbs.c
)
if(EMSCRIPTEN)
    # No -pthread on the web: it would need SharedArrayBuffer and special
    # server headers. draw_mt.c is not used there (pthread_create fails and
    # it falls back to one thread).
    target_link_libraries(rolling_rob_game PUBLIC lvgl m)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(rolling_rob_game PUBLIC lvgl m Threads::Threads)
endif()
target_include_directories(rolling_rob_game PUBLIC src/game)
# Warnings for our own code (not LVGL's); ROLLING_ROB_WERROR makes them
# errors (CI does).
option(ROLLING_ROB_WERROR "Treat warnings in Rolling Rob's own code as errors" OFF)
set(RR_WARNINGS -Wall -Wextra -Wno-unused-parameter)
if(ROLLING_ROB_WERROR)
    list(APPEND RR_WARNINGS -Werror)
endif()
target_compile_options(rolling_rob_game PRIVATE ${RR_WARNINGS})

set(RR_COMMON_DEFS
    ROLLING_ROB_SCREEN_WIDTH=${ROLLING_ROB_SCREEN_WIDTH}
    ROLLING_ROB_SCREEN_HEIGHT=${ROLLING_ROB_SCREEN_HEIGHT}
    ROLLING_ROB_VIEW_SCALE=${ROLLING_ROB_VIEW_SCALE}f
)

# The app around the game (src/app: menu, Tetris, 2048, info page), on every
# platform; each brings its own info page (app_info_*.c).
set(RR_APP_SOURCES src/app/app.c src/app/tetris_view.c src/tetris/tetris.c
                   src/app/g2048_view.c src/g2048/g2048.c
                   src/app/pix.c src/app/game_ui.c src/app/app_page.c
                   src/app/snake_view.c src/snake/snake.c
                   src/app/breakout_view.c src/breakout/breakout.c
                   src/app/mines_view.c src/mines/mines.c
                   src/app/world3d_view.c src/app/rob3d_view.c src/r3d/r3d.c
                   src/app/fractal_view.c src/fractal/fractal.c)

add_executable(rolling_rob_sim src/platform/sim/main_sim.c src/platform/sim/bot.c
               src/platform/sim/music.c src/platform/sim/app_info_sdl.c
               ${RR_APP_SOURCES})
target_compile_options(rolling_rob_sim PRIVATE ${RR_WARNINGS})
target_include_directories(rolling_rob_sim PRIVATE third_party/miniaudio src/app src/tetris src/g2048 src/r3d src/snake src/breakout src/mines src/fractal src/platform/sim)
# Which build this is, for the app's info page: the release tag (passed by
# tools/build-web.sh, else git describe), the commit and the build time.
# Only app_info_sdl.c gets them, so a new value rebuilds that one file.
if(NOT ROLLING_ROB_VERSION)
    execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE ROLLING_ROB_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT ROLLING_ROB_VERSION)
    set(ROLLING_ROB_VERSION "dev")
endif()
execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE RR_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT RR_COMMIT)
    set(RR_COMMIT "unknown")
endif()
if(NOT ROLLING_ROB_BUILT)
    string(TIMESTAMP ROLLING_ROB_BUILT "%d %b %Y %H:%M UTC" UTC)
endif()
set_source_files_properties(src/platform/sim/app_info_sdl.c PROPERTIES COMPILE_DEFINITIONS
    "RR_VERSION=\"${ROLLING_ROB_VERSION}\";RR_COMMIT=\"${RR_COMMIT}\";RR_BUILT=\"${ROLLING_ROB_BUILT}\"")
message(STATUS "Rolling Rob ${ROLLING_ROB_VERSION} (${RR_COMMIT}), built ${ROLLING_ROB_BUILT}")

target_compile_definitions(rolling_rob_sim PRIVATE
    ROLLING_ROB_TARGET_SIM=1
    ROLLING_ROB_SIM_ZOOM=${ROLLING_ROB_SIM_ZOOM}
    ${RR_COMMON_DEFS}
)
target_compile_definitions(lvgl PUBLIC ROLLING_ROB_TARGET_SIM=1)
if(EMSCRIPTEN)
    # WebAssembly: SDL2 from Emscripten's ports; art and music are packed
    # into index.data and appear at /art and /music/mp3; the page is
    # web/shell.html. Build: tools/build-web.sh
    target_compile_options(lvgl PUBLIC -sUSE_SDL=2)
    target_link_libraries(rolling_rob_sim PRIVATE rolling_rob_game)
    set(RR_WEB_DATA ${CMAKE_BINARY_DIR}/webdata)
    file(REMOVE_RECURSE ${RR_WEB_DATA})
    file(COPY ${CMAKE_SOURCE_DIR}/art DESTINATION ${RR_WEB_DATA})
    if(EXISTS ${CMAKE_SOURCE_DIR}/music/mp3)
        file(COPY ${CMAKE_SOURCE_DIR}/music/mp3 DESTINATION ${RR_WEB_DATA}/music)
    endif()
    target_link_options(rolling_rob_sim PRIVATE
        -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=128MB -sSTACK_SIZE=1MB
        -sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency,callMain,ENV
        -lidbfs.js
        -sEXPORTED_FUNCTIONS=_main,_emscripten_set_main_loop_timing,_emscripten_pause_main_loop,_emscripten_resume_main_loop
        --preload-file webdata@/
        --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html)
    set_target_properties(rolling_rob_sim PROPERTIES OUTPUT_NAME index SUFFIX ".html"
        LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html)
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(lvgl PUBLIC SDL2::SDL2)
    target_link_libraries(rolling_rob_sim PRIVATE rolling_rob_game SDL2::SDL2)
endif()
if(WIN32)
    # Windows (mingw-w64 cross build): a GUI program linked fully static.
    target_link_options(rolling_rob_sim PRIVATE -mwindows -static -static-libgcc)
    set_target_properties(rolling_rob_sim PROPERTIES OUTPUT_NAME RollingRob)
endif()

# Unit tests for the rules of the games (tests/; no LVGL or SDL needed).
# ctest --test-dir build --output-on-failure
option(ROLLING_ROB_TESTS "Build the unit tests" ON)
if(ROLLING_ROB_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()
