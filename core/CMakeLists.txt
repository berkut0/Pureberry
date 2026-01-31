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
option(ENABLE_USB_AUDIO "Enable USB Audio device mode (UAC2 speaker input)" OFF)
option(ENABLE_OLED "Enable SSD1306 OLED (I2C) support" ON)
option(ENABLE_I2C_DMA "Enable non-blocking I2C TX via DMA helper" OFF)

# Add executable
add_executable(${PROJECT_NAME}.elf)

# Mark Heavy-generated code as bare-metal to avoid platform warnings
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE HV_BARE_METAL)

# Make project config visible to pico-sdk (and thus to pico-extras sources too)
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE PICO_CONFIG_HEADER=pico_config.h)

# Optional I2C DMA helper (device drivers may use it to avoid blocking I2C writes)
if(ENABLE_I2C_DMA)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ENABLE_I2C_DMA)
    target_sources(${PROJECT_NAME}.elf PRIVATE
        src/drv/i2c_dma.c
    )
endif()

# Core firmware source files
target_sources(${PROJECT_NAME}.elf PRIVATE
    src/main.c
    src/multicore_audio.c
    src/patch_api.c
    src/crash.c
    src/dev/adc_pots.c
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

# Optional SSD1306 OLED (u8g2) support
if(ENABLE_OLED)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
        ENABLE_OLED
        U8X8_WITH_USER_PTR
    )

    set(U8G2_CSRC_DIR "${PROJECT_ROOT}/third_party/u8g2/csrc")

    target_sources(${PROJECT_NAME}.elf PRIVATE
        src/dev/oled.c
        src/dev/u8g2_pico.c
        src/multicore_display.c

        # u8g2 core (subset)
        ${U8G2_CSRC_DIR}/u8g2_arc.c
        ${U8G2_CSRC_DIR}/u8g2_bitmap.c
        ${U8G2_CSRC_DIR}/u8g2_box.c
        ${U8G2_CSRC_DIR}/u8g2_buffer.c
        ${U8G2_CSRC_DIR}/u8g2_circle.c
        ${U8G2_CSRC_DIR}/u8g2_cleardisplay.c
        ${U8G2_CSRC_DIR}/u8g2_d_memory.c
        ${U8G2_CSRC_DIR}/u8g2_d_setup.c
        ${U8G2_CSRC_DIR}/u8g2_font.c
        ${U8G2_CSRC_DIR}/u8g2_fonts.c
        ${U8G2_CSRC_DIR}/u8g2_hvline.c
        ${U8G2_CSRC_DIR}/u8g2_intersection.c
        ${U8G2_CSRC_DIR}/u8g2_kerning.c
        ${U8G2_CSRC_DIR}/u8g2_line.c
        ${U8G2_CSRC_DIR}/u8g2_ll_hvline.c
        ${U8G2_CSRC_DIR}/u8g2_message.c
        ${U8G2_CSRC_DIR}/u8g2_polygon.c
        ${U8G2_CSRC_DIR}/u8g2_setup.c

        # u8x8 core + SSD1306 driver
        ${U8G2_CSRC_DIR}/u8x8_8x8.c
        ${U8G2_CSRC_DIR}/u8x8_byte.c
        ${U8G2_CSRC_DIR}/u8x8_cad.c
        ${U8G2_CSRC_DIR}/u8x8_display.c
        ${U8G2_CSRC_DIR}/u8x8_fonts.c
        ${U8G2_CSRC_DIR}/u8x8_gpio.c
        ${U8G2_CSRC_DIR}/u8x8_message.c
        ${U8G2_CSRC_DIR}/u8x8_setup.c
        ${U8G2_CSRC_DIR}/u8x8_string.c
        ${U8G2_CSRC_DIR}/u8x8_u16toa.c
        ${U8G2_CSRC_DIR}/u8x8_u8toa.c
        ${U8G2_CSRC_DIR}/u8x8_d_ssd1306_128x64_noname.c
    )

    target_include_directories(${PROJECT_NAME}.elf PRIVATE
        ${U8G2_CSRC_DIR}
    )
endif()

# Include directories
target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/dev
)

# USB configuration: either pico_stdio_usb (default) or custom TinyUSB (MIDI and/or Audio)
if(ENABLE_USB_MIDI OR ENABLE_USB_AUDIO)
    # Custom TinyUSB for composite USB device (CDC + optional MIDI + optional Audio)
    
    # Enable TinyUSB device library (this sets up all the necessary flags)
    set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
    
    # Define that we're using TinyUSB device mode
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE 
        LIB_TINYUSB_DEVICE=1
    )

    if(ENABLE_USB_MIDI)
        target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ENABLE_USB_MIDI)
    endif()

    if(ENABLE_USB_AUDIO)
        target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ENABLE_USB_AUDIO)
    endif()
    
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
    )

    if(ENABLE_USB_MIDI)
        target_sources(${PROJECT_NAME}.elf PRIVATE
            src/usb/usb_midi.c
        )
    endif()

    if(ENABLE_USB_AUDIO)
        target_sources(${PROJECT_NAME}.elf PRIVATE
            src/usb/usb_audio.c
        )
    endif()
    
    if(ENABLE_USB_MIDI AND ENABLE_USB_AUDIO)
        message(STATUS "USB enabled: Using custom TinyUSB (CDC+MIDI+Audio)")
    elseif(ENABLE_USB_MIDI)
        message(STATUS "USB enabled: Using custom TinyUSB (CDC+MIDI)")
    elseif(ENABLE_USB_AUDIO)
        message(STATUS "USB enabled: Using custom TinyUSB (CDC+Audio)")
    endif()
else()
    # Default: use pico_stdio_usb (CDC only, simpler)
    pico_enable_stdio_usb(${PROJECT_NAME}.elf 1)
    message(STATUS "USB custom stack disabled: Using pico_stdio_usb (CDC only)")
endif()

# Link libraries (pico_multicore for multicore_launch_core1; pico_util for queue_t; hardware_adc for pots)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    pico_stdlib
    pico_sync
    pico_multicore
    pico_util
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_clocks
    hardware_pio
    hardware_irq
    hardware_adc
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
