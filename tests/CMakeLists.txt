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

# UngulaCore (time API) and EmblogX are sibling libraries during local
# development. The multiplexer test target needs both: i_multiplexer.cpp
# uses log_*_m macros and pulls TimeControl in via the TCA9548 driver.
set(LIB_CORE_SRC ${CMAKE_CURRENT_LIST_DIR}/../../lib/src)
set(LIB_EMBLOGX_SRC ${CMAKE_CURRENT_LIST_DIR}/../../lib_emblogx/src)

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${LIB_HAL_SRC}
    ${LIB_CORE_SRC}
    ${LIB_EMBLOGX_SRC}
)

add_compile_definitions(
    EMBLOGX_BACKEND_STDIO
    EMBLOGX_LOG_PREFIX="[TEST] "
    EMBLOGX_ENABLE_SINK_CONSOLE=1
    EMBLOGX_ENABLE_SINK_MEMORY=0
    EMBLOGX_ENABLE_SINK_HTTP=0
    EMBLOGX_ENABLE_SINK_SD=0
)

# ---------------------------------------------------------------------------
# HAL stub tests — verify desktop stubs compile and behave correctly.
# No hardware needed: these exercise the default (non-ESP) implementations.
# ---------------------------------------------------------------------------
add_executable(test_ungula_hal
    test_gpio_access.cpp
    test_i2c_master.cpp
    test_spi_master.cpp
    test_uart.cpp
    test_adc_manager.cpp
    test_can.cpp
    ${LIB_HAL_SRC}/ungula/hal/i2c/platforms/i2c_master_default.cpp
    ${LIB_HAL_SRC}/ungula/hal/spi/platforms/spi_master_default.cpp
    ${LIB_HAL_SRC}/ungula/hal/uart/platforms/uart_default.cpp
    ${LIB_HAL_SRC}/ungula/hal/can/platforms/can_default.cpp
)
target_link_libraries(test_ungula_hal gtest_main)

# ---------------------------------------------------------------------------
# Multiplexer tests — host-only, no I2C hardware. Exercises the IMultiplexer
# base behaviour (cache, retry, restart, logging gate) through the
# MultiplexerFake driver shipped under drivers/.
# ---------------------------------------------------------------------------
add_executable(test_ungula_multiplexer
    test_multiplexer.cpp
    ${LIB_HAL_SRC}/ungula/hal/multiplexer/i_multiplexer.cpp
    ${LIB_EMBLOGX_SRC}/emblogx/logger_core.cpp
    ${LIB_EMBLOGX_SRC}/emblogx/async_dispatcher.cpp
    ${LIB_EMBLOGX_SRC}/emblogx/sinks/console_sink.cpp
)
target_link_libraries(test_ungula_multiplexer gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(test_ungula_hal)
gtest_discover_tests(test_ungula_multiplexer)
