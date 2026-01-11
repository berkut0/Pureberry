cmake_minimum_required(VERSION 3.13)

# Include pico_sdk_import.cmake from the project root
include(pico_sdk_import.cmake)

# Include pico_extras_import.cmake for audio I2S support
include(pico_extras_import.cmake)

project(rp2350_puredata_firmware)

# Initialize the Pico SDK
pico_sdk_init()

# Set C/C++ standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Optional features
option(ENABLE_WS2812 "Enable WS2812 LED support" ON)
option(ENABLE_USB_MIDI "Enable USB MIDI support (CDC+MIDI composite device)" OFF)

# Add executable
add_executable(${PROJECT_NAME}.elf)

# Core firmware source files
target_sources(${PROJECT_NAME}.elf PRIVATE
    src/main.c
)

# Optional WS2812 support
if(ENABLE_WS2812)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ENABLE_WS2812)
    target_sources(${PROJECT_NAME}.elf PRIVATE
        src/dev/ws2812.c
    )
    # Generate PIO header
    pico_generate_pio_header(${PROJECT_NAME}.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/src/dev/ws2812.pio
    )
endif()

# Heavy-generated source files will be added here by build script
# They should be in the build directory under c/
# Example:
# target_sources(${PROJECT_NAME}.elf PRIVATE
#     ${HEAVY_SOURCE_DIR}/Heavy_heavy.cpp
#     ${HEAVY_SOURCE_DIR}/HeavyContext.cpp
#     ${HEAVY_SOURCE_DIR}/HvControl*.c
#     ${HEAVY_SOURCE_DIR}/HvSignal*.c
#     ${HEAVY_SOURCE_DIR}/HvMessage*.c
#     ${HEAVY_SOURCE_DIR}/HvUtils.c
#     ${HEAVY_SOURCE_DIR}/HvTable.c
#     ${HEAVY_SOURCE_DIR}/HvLightPipe.c
#     ${HEAVY_SOURCE_DIR}/HvHeavy.cpp
# )

# Include directories
target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/dev
    # Heavy-generated headers will be added here
    # ${HEAVY_SOURCE_DIR}
)

# USB configuration: either pico_stdio_usb (default) or custom TinyUSB (MIDI enabled)
if(ENABLE_USB_MIDI)
    # Custom TinyUSB for CDC+MIDI composite device
    
    # Enable TinyUSB device library (this sets up all the necessary flags)
    set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
    
    # Define that we're using TinyUSB device mode
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE 
        ENABLE_USB_MIDI
        LIB_TINYUSB_DEVICE=1
        CFG_TUSB_MCU=OPT_MCU_RP2040
        CFG_TUSB_OS=OPT_OS_NONE
    )
    
    # Include tusb_config.h from src directory
    target_include_directories(${PROJECT_NAME}.elf PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}/src
    )
    
    # Link TinyUSB device stack
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE
        tinyusb_device
        tinyusb_board
    )
    
    # USB source files
    target_sources(${PROJECT_NAME}.elf PRIVATE
        src/usb/usb_descriptors.c
        src/usb/usb_cdc.c
        src/usb/usb_midi.c
    )
    
    message(STATUS "USB MIDI enabled: Using custom TinyUSB (CDC+MIDI)")
else()
    # Default: use pico_stdio_usb (CDC only, simpler)
    pico_enable_stdio_usb(${PROJECT_NAME}.elf 1)
    message(STATUS "USB MIDI disabled: Using pico_stdio_usb (CDC only)")
endif()

# Link libraries
target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    pico_stdlib
    pico_sync
    hardware_dma
    hardware_pwm
    hardware_clocks
    hardware_pio
    hardware_irq
    pico_audio
    pico_audio_i2s
)

# Disable UART stdio (use USB only)
pico_enable_stdio_uart(${PROJECT_NAME}.elf 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME}.elf)

# Print information
message(STATUS "Pico SDK path: ${PICO_SDK_PATH}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
