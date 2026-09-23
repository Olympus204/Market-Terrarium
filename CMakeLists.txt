cmake_minimum_required(VERSION 3.20)

project(market_terrarium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

add_library(market_core
    src/order_book.cpp
    src/market.cpp
    src/trader.cpp
    src/simulation.cpp
    src/decisions.cpp
)

target_include_directories(market_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(market_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_library(gui_support
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp

    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp

    external/implot/implot.cpp
    external/implot/implot_items.cpp
)

target_include_directories(gui_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/external/implot
)

target_link_libraries(gui_support PUBLIC
    glfw
    OpenGL::GL
)

add_executable(market_terrarium
    src/main.cpp
)

target_link_libraries(market_terrarium PRIVATE
    market_core
    gui_support
)

add_executable(tests
    tests/test_main.cpp
    tests/test_utils.cpp
    tests/order_book_tests.cpp
    tests/market_tests.cpp
    tests/trader_tests.cpp
    tests/simulation_tests.cpp
)

target_link_libraries(tests PRIVATE market_core)

target_compile_options(tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)