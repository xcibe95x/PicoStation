cmake_minimum_required(VERSION 3.24.1)

include(pico_sdk_import.cmake)

project(picostation)

pico_sdk_init()

add_executable(picostation)

target_sources(picostation PRIVATE cmd.cpp disc_image.cpp hw_config.cpp i2s.cpp main.cpp subq.cpp utils.cpp)

add_subdirectory(no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)

pico_enable_stdio_usb(picostation 1)

pico_generate_pio_header(picostation ${CMAKE_CURRENT_LIST_DIR}/main.pio)

target_link_libraries(picostation PRIVATE FatFs_SPI hardware_dma hardware_pio hardware_pwm hardware_vreg pico_multicore pico_stdlib)

#pico_set_binary_type(picostation copy_to_ram)

pico_add_extra_outputs(picostation)
