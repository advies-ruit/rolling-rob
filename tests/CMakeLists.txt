# Unit tests for the rules of the games (src/tetris, g2048, snake, breakout,
# mines, fractal). They need neither LVGL nor SDL, so they also build on
# their own:
#   cmake -S tests -B build-tests && cmake --build build-tests -j
#   ctest --test-dir build-tests --output-on-failure
# From the top-level project they come along with BUILD_TESTING (on by
# default, off for the web build).

cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(rolling_rob_tests C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    enable_testing()
endif()

set(RR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

function(rr_test name)
    add_executable(test_${name} test_${name}.c ${ARGN})
    target_include_directories(test_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_${name} PRIVATE -Wall -Wextra -Wno-unused-parameter -Werror -UNDEBUG)
    target_link_libraries(test_${name} PRIVATE m)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

rr_test(g2048 ${RR_SRC}/g2048/g2048.c)
target_include_directories(test_g2048 PRIVATE ${RR_SRC}/g2048)
rr_test(tetris ${RR_SRC}/tetris/tetris.c)
target_include_directories(test_tetris PRIVATE ${RR_SRC}/tetris)
rr_test(snake ${RR_SRC}/snake/snake.c)
target_include_directories(test_snake PRIVATE ${RR_SRC}/snake)
rr_test(breakout ${RR_SRC}/breakout/breakout.c)
target_include_directories(test_breakout PRIVATE ${RR_SRC}/breakout)
rr_test(mines ${RR_SRC}/mines/mines.c)
target_include_directories(test_mines PRIVATE ${RR_SRC}/mines)
target_compile_definitions(test_mines PRIVATE MI_CHECK) # the solver checks itself
rr_test(fractal ${RR_SRC}/fractal/fractal.c)
target_include_directories(test_fractal PRIVATE ${RR_SRC}/fractal)
