cmake_minimum_required(VERSION 3.24.1)

include(pico_sdk_import.cmake)

project(picostation)

pico_sdk_init()

add_executable(picostation)

target_sources(picostation PRIVATE
    src/cmd.cpp
    src/disc_image.cpp
    src/hw_config.cpp
    src/i2s.cpp
    src/main.cpp
    src/picostation.cpp
    src/subq.cpp
    src/utils.cpp

    third_party/cueparser/cueparser.c
    third_party/cueparser/fileabstract.c
    third_party/cueparser/scheduler.c
    third_party/posix_file.c
)

target_include_directories(picostation PRIVATE
    third_party
)

add_subdirectory(third_party/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)

pico_enable_stdio_usb(picostation 1)

pico_generate_pio_header(picostation ${CMAKE_CURRENT_LIST_DIR}/pio/main.pio)

target_link_libraries(picostation PRIVATE FatFs_SPI hardware_dma hardware_pio hardware_pwm hardware_vreg pico_multicore pico_stdlib)

#pico_set_binary_type(picostation copy_to_ram)

pico_add_extra_outputs(picostation)
