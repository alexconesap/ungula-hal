cmake_minimum_required(VERSION 3.16)
project(ungula_hal_lib_tests)

set(CMAKE_CXX_STANDARD 17)

set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_LIST_DIR}/vendor)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

set(LIB_HAL_SRC ${CMAKE_CURRENT_LIST_DIR}/../src)

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${LIB_HAL_SRC}
)

# ---------------------------------------------------------------------------
# HAL stub tests — verify desktop stubs compile and behave correctly.
# No hardware needed: these exercise the default (non-ESP) implementations.
# ---------------------------------------------------------------------------
add_executable(test_ungula_hal
    test_i2c_master.cpp
    test_spi_master.cpp
    test_uart.cpp
    ${LIB_HAL_SRC}/hal/i2c/platforms/i2c_master_default.cpp
    ${LIB_HAL_SRC}/hal/spi/platforms/spi_master_default.cpp
    ${LIB_HAL_SRC}/hal/uart/platforms/uart_default.cpp
)
target_link_libraries(test_ungula_hal gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(test_ungula_hal)
