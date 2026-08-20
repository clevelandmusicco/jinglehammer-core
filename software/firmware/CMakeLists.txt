# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(midi_ctrl C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(midi_ctrl
    main.c
    config.c
    midi.c
    io.c
    controller.c
    cdc_proto.c
    usb_descriptors.c
    usb_reset_iface.c
)

target_include_directories(midi_ctrl PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(midi_ctrl
    pico_stdlib
    hardware_uart
    hardware_flash
    hardware_sync
    hardware_pwm
    hardware_watchdog
    pico_bootrom
    pico_usb_reset_interface_headers
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(midi_ctrl)
