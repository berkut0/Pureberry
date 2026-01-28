cmake_minimum_required(VERSION 3.13)

# Set picotool path before including SDK files (Findpicotool.cmake is called during pico_sdk_init())
# PROJECT_ROOT can be passed via -D from the build script; otherwise fall back to repo layout.
set(PROJECT_ROOT "" CACHE PATH "Project root (optional)")
if(PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" ABSOLUTE)
else()
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(LOCAL_PICOTOOL_PATH "${PROJECT_ROOT}/third_party/picotool")

if(NOT DEFINED PICOTOOL_FETCH_FROM_GIT_PATH AND
   (EXISTS "${LOCAL_PICOTOOL_PATH}/.git" OR EXISTS "${LOCAL_PICOTOOL_PATH}/CMakeLists.txt"))
    file(TO_CMAKE_PATH "${LOCAL_PICOTOOL_PATH}" LOCAL_PICOTOOL_PATH_CMAKE)
    set(PICOTOOL_FETCH_FROM_GIT_PATH "${LOCAL_PICOTOOL_PATH_CMAKE}" CACHE PATH "Local picotool path")
    message(STATUS "Using local picotool from submodule: ${LOCAL_PICOTOOL_PATH_CMAKE}")
endif()

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

# Mark Heavy-generated code as bare-metal to avoid platform warnings
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE HV_BARE_METAL)

# Make project config visible to pico-sdk (and thus to pico-extras sources too)
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE PICO_CONFIG_HEADER=pico_config.h)

# Core firmware source files
target_sources(${PROJECT_NAME}.elf PRIVATE
    src/main.c
)

# Heavy-generated source files (optional; prefer manifest from build script)
set(HEAVY_SOURCES_FILE "" CACHE FILEPATH "Path to Heavy sources manifest (optional)")
set(HEAVY_SOURCE_DIR "" CACHE PATH "Path to Heavy-generated sources (optional)")

if(HEAVY_SOURCES_FILE)
    if(EXISTS "${HEAVY_SOURCES_FILE}")
        include("${HEAVY_SOURCES_FILE}")
    else()
        message(FATAL_ERROR "HEAVY_SOURCES_FILE does not exist: ${HEAVY_SOURCES_FILE}")
    endif()
endif()

if(NOT HEAVY_SOURCES AND HEAVY_SOURCE_DIR)
    file(GLOB CONFIGURE_DEPENDS HEAVY_C_FILES "${HEAVY_SOURCE_DIR}/*.c")
    file(GLOB CONFIGURE_DEPENDS HEAVY_CPP_FILES "${HEAVY_SOURCE_DIR}/*.cpp")
    if(NOT HEAVY_C_FILES AND NOT HEAVY_CPP_FILES)
        message(FATAL_ERROR "HEAVY_SOURCE_DIR is set but no Heavy sources found: ${HEAVY_SOURCE_DIR}")
    endif()
    set(HEAVY_SOURCES ${HEAVY_C_FILES} ${HEAVY_CPP_FILES})
    set(HEAVY_INCLUDE_DIRS ${HEAVY_SOURCE_DIR})
endif()

if(HEAVY_SOURCES)
    target_sources(${PROJECT_NAME}.elf PRIVATE
        ${HEAVY_SOURCES}
    )
    if(HEAVY_INCLUDE_DIRS)
        target_include_directories(${PROJECT_NAME}.elf PRIVATE
            ${HEAVY_INCLUDE_DIRS}
        )
    endif()
elseif(HEAVY_SOURCES_FILE OR HEAVY_SOURCE_DIR)
    message(FATAL_ERROR "Heavy sources requested but none found")
endif()

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

# Include directories
target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/dev
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
