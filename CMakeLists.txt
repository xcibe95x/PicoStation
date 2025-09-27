# == To use only if the uf2 is not booting into picostation ==
option(BOOT_PATCHING "Patch Boot if produced uf2 is not booting up correctly" ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/")

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Example override variant
# set(PICOSTATION_VARIANT "picostation_plus_pico2")

if(NOT DEFINED PICOSTATION_VARIANT)
    if(PICO_BOARD EQUAL pico2)
        set(PICOSTATION_VARIANT "picostation_pico2")
    else()
        set(PICOSTATION_VARIANT "picostation_pico1")
    endif()
    message(STATUS "NOTE: PicoStation variant not defined. Using ${PICOSTATION_VARIANT}")
endif()

include(boards/picostation_variant.cmake)

cmake_minimum_required(VERSION 3.24.1)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(pico_sdk_import.cmake)

project(${PROJECT_NAME})

pico_sdk_init()

set(CMAKE_BUILD_TYPE Release)

function(addBinaryFileWithSize target name sizeName path)
    set(asmFile "${PROJECT_BINARY_DIR}/includes/${target}_${name}.s")
    cmake_path(ABSOLUTE_PATH path OUTPUT_VARIABLE fullPath)

    file(
        CONFIGURE
        OUTPUT "${asmFile}"
        CONTENT [[
            .section .rodata.${name}, "a"
            .balign 8

            .global ${name}
            .type ${name}, %object
            .size ${name}, (${name}_end - ${name})

            ${name}:
                .incbin "${fullPath}"
            ${name}_end:

            .section .rodata.${sizeName}, "a"
            .balign 4

            .global ${sizeName}
            .type ${sizeName}, %object
            .size ${sizeName}, 4

            ${sizeName}:
                .int (${name}_end - ${name})
        ]]
        ESCAPE_QUOTES
        NEWLINE_STYLE LF
    )

    target_sources(${target} PRIVATE "${asmFile}")
    set_source_files_properties(
        "${asmFile}" PROPERTIES OBJECT_DEPENDS "${fullPath}"
    )
endfunction()

add_executable(${PROJECT_NAME})

target_compile_definitions(
    ${PROJECT_NAME} PUBLIC
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    MAXINDEX=2
)

addBinaryFileWithSize(${PROJECT_NAME} loaderImage loaderImageSize binary/picostation-menu.bin)

target_sources(
    ${PROJECT_NAME} PRIVATE
    src/cmd.cpp
    src/disc_image.cpp
    src/drive_mechanics.cpp
    src/firmware_update.cpp
    src/i2s.cpp
    src/main.cpp
    src/modchip.cpp
    src/picostation.cpp
    src/subq.cpp
    src/directory_listing.cpp
    src/si5351.c
    third_party/cueparser/cueparser.c
    third_party/cueparser/fileabstract.c
    third_party/cueparser/scheduler.c
    third_party/posix_file.c
)

target_include_directories(
    ${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/third_party
)

add_subdirectory(third_party/SD-fatfs build)
add_subdirectory(third_party/RP2040_Pseudo_Atomic)

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/pio/main.pio)

target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_vreg
    hardware_i2c
    SD-fatfs
    pico_bootrom
    pico_multicore
    pico_stdlib
)

target_include_directories(SD-fatfs BEFORE INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/source
)

pico_add_extra_outputs(${PROJECT_NAME})

# --- Post-build UF2 byte patch (robust) ---
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(UF2_FILE "${CMAKE_BINARY_DIR}/${PICOSTATION_VARIANT}.uf2")
set(PATCH_SCRIPT "${CMAKE_SOURCE_DIR}/tools/patch_boot.py")
set(PATCH_STAMP  "${CMAKE_BINARY_DIR}/${PICOSTATION_VARIANT}.uf2.patched.stamp")

if (BOOT_PATCHING AND EXISTS "${PATCH_SCRIPT}")
    # Generate a stamp only after UF2 is created and patch script runs
    add_custom_command(
        OUTPUT  "${PATCH_STAMP}"
        COMMAND "${Python3_EXECUTABLE}" "${PATCH_SCRIPT}" "${UF2_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${PATCH_STAMP}"
        DEPENDS "${UF2_FILE}"
        COMMENT "Patching UF2 (in-place) and creating stamp"
        VERBATIM
    )

    # Make a target that depends on the stamp
    add_custom_target(${PROJECT_NAME}_red ALL DEPENDS "${PATCH_STAMP}")

else()
    message(WARNING "patch_boot.py not found at ${PATCH_SCRIPT}; skipping UF2 patch step.")
endif()