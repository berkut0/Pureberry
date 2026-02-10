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

# IMPORTANT:
# Do NOT set PICOTOOL_FETCH_FROM_GIT_PATH to the picotool submodule root.
# Pico SDK's Findpicotool.cmake uses PICOTOOL_FETCH_FROM_GIT_PATH as an *install/build directory*
# and will create picotool-src/, picotool-build/, picotool-subbuild/, picotool/ under it.
# If pointed at the submodule, the submodule becomes permanently "dirty" (untracked build artifacts).
#
# Instead, keep using the local submodule as the git source, but put build/install output under repo build/
# which is already ignored.
if(NOT DEFINED PICOTOOL_GIT_REPOSITORY_URL AND
   (EXISTS "${LOCAL_PICOTOOL_PATH}/.git" OR EXISTS "${LOCAL_PICOTOOL_PATH}/CMakeLists.txt"))
    file(TO_CMAKE_PATH "${LOCAL_PICOTOOL_PATH}" LOCAL_PICOTOOL_PATH_CMAKE)
    set(PICOTOOL_GIT_REPOSITORY_URL "${LOCAL_PICOTOOL_PATH_CMAKE}" CACHE STRING "Picotool git repository (local)" FORCE)
endif()

if(NOT DEFINED PICOTOOL_FETCH_FROM_GIT_PATH)
    file(TO_CMAKE_PATH "${PROJECT_ROOT}/build/_picotool" PICOTOOL_BUILD_ROOT_CMAKE)
    set(PICOTOOL_FETCH_FROM_GIT_PATH "${PICOTOOL_BUILD_ROOT_CMAKE}" CACHE PATH "Picotool build/install dir" FORCE)
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
option(ENABLE_USB_AUDIO "Enable USB Audio support (CDC+Audio composite device)" OFF)
option(ENABLE_OLED "Enable SSD1306 OLED (I2C) support" OFF)
option(ENABLE_I2C_DMA "Enable non-blocking I2C TX via DMA helper" OFF)
option(ENABLE_MPR121 "Enable MPR121 capacitive touch (I2C) support" OFF)
set(
    FW_SYS_CLOCK_PROFILE_OVERRIDE
    ""
    CACHE STRING
    "Optional override for FW_SYS_CLOCK_PROFILE macro (empty: use config.h/config_local.h)"
)

# Add executable
add_executable(${PROJECT_NAME}.elf)

# Mark Heavy-generated code as bare-metal to avoid platform warnings
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE HV_BARE_METAL)

# Make project config visible to pico-sdk (and thus to pico-extras sources too)
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE PICO_CONFIG_HEADER=pico_config.h)

if(NOT "${FW_SYS_CLOCK_PROFILE_OVERRIDE}" STREQUAL "")
    target_compile_definitions(
        ${PROJECT_NAME}.elf PRIVATE FW_SYS_CLOCK_PROFILE=${FW_SYS_CLOCK_PROFILE_OVERRIDE}
    )
    message(STATUS "FW_SYS_CLOCK_PROFILE override: ${FW_SYS_CLOCK_PROFILE_OVERRIDE}")
endif()

# Optional firmware log level (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
set(FIRMWARE_LOG_LEVEL "" CACHE STRING "Firmware log level (0..3)")
if(NOT "${FIRMWARE_LOG_LEVEL}" STREQUAL "")
    if(NOT FIRMWARE_LOG_LEVEL MATCHES "^[0-3]$")
        message(FATAL_ERROR "FIRMWARE_LOG_LEVEL must be 0..3 (got: '${FIRMWARE_LOG_LEVEL}')")
    endif()
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE FIRMWARE_LOG_LEVEL=${FIRMWARE_LOG_LEVEL})
endif()

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
    src/audio_runtime.c
    src/crosscore_bus.c
    src/patch_api.c
    src/crash.c
    src/input/ui_input.c
    src/input/ui_input_multibutton.c
    src/input/ui_action_map.c
    src/ui/ui_manager.c
    src/ui/system/ui_system_stats.c
    src/ui/mui/ui_mui_runtime.c
    src/ui/mui/ui_mui_forms.c
    src/ui/screens/ui_screen_main_menu.c
    src/ui/screens/ui_screen_waveform.c
    src/drv/i2c_bus.c
    src/drv/spi_bus.c
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

# Optional MPR121 capacitive touch (pico-mpr121 from third_party)
set(PICO_MPR121_PATH "${PROJECT_ROOT}/third_party/pico-mpr121")
if(ENABLE_MPR121 AND EXISTS "${PICO_MPR121_PATH}/lib/CMakeLists.txt")
    add_subdirectory(${PICO_MPR121_PATH}/lib mpr121)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ENABLE_MPR121)
    target_sources(${PROJECT_NAME}.elf PRIVATE
        src/dev/mpr121_touch.c
    )
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE pico-mpr121)
    message(STATUS "MPR121 touch support enabled (pico-mpr121)")
elseif(ENABLE_MPR121)
    message(WARNING "ENABLE_MPR121 is ON but third_party/pico-mpr121 not found; run git submodule update --init --recursive")
endif()

# MultiButton input backend (third_party)
set(MULTIBUTTON_PATH "${PROJECT_ROOT}/third_party/multibutton")
if(EXISTS "${MULTIBUTTON_PATH}/multi_button.c" AND EXISTS "${MULTIBUTTON_PATH}/multi_button.h")
    target_sources(${PROJECT_NAME}.elf PRIVATE
        ${MULTIBUTTON_PATH}/multi_button.c
    )
    target_include_directories(${PROJECT_NAME}.elf PRIVATE
        ${MULTIBUTTON_PATH}
    )
else()
    message(WARNING "third_party/multibutton not found; forcing UI_INPUT_ENABLED=0")
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE UI_INPUT_ENABLED=0)
endif()

# Optional SSD1306 OLED (u8g2) support
if(ENABLE_OLED)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
        ENABLE_OLED
        U8X8_WITH_USER_PTR
    )

    set(U8G2_CSRC_DIR "${PROJECT_ROOT}/third_party/u8g2/csrc")
    include(${CMAKE_CURRENT_LIST_DIR}/u8g2_sources.cmake)

    target_sources(${PROJECT_NAME}.elf PRIVATE
        src/dev/oled.c
        src/dev/u8g2_pico.c
        src/multicore_display.c
        ${U8G2_CORE_SOURCES}
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

# USB configuration: always use TinyUSB device stack, with optional MIDI/Audio interfaces.
#
# Rationale:
# - pico_stdio_usb owns USB (descriptors, background tud_task) and does not scale to composite devices.
# - Our firmware must use one stable USB stack in all builds; feature flags only toggle interfaces.
target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    tinyusb_device
    tinyusb_board
)

target_sources(${PROJECT_NAME}.elf PRIVATE
    src/usb/usb_descriptors.c
    src/usb/usb_cdc.c
)

if(ENABLE_USB_MIDI)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ENABLE_USB_MIDI)
    target_sources(${PROJECT_NAME}.elf PRIVATE src/usb/usb_midi.c)
endif()

if(ENABLE_USB_AUDIO)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ENABLE_USB_AUDIO)
    target_sources(${PROJECT_NAME}.elf PRIVATE src/usb/usb_audio.c)
endif()

if(ENABLE_USB_MIDI AND ENABLE_USB_AUDIO)
    message(STATUS "USB enabled: TinyUSB composite (CDC+MIDI+Audio)")
elseif(ENABLE_USB_MIDI)
    message(STATUS "USB enabled: TinyUSB composite (CDC+MIDI)")
elseif(ENABLE_USB_AUDIO)
    message(STATUS "USB enabled: TinyUSB composite (CDC+Audio)")
else()
    message(STATUS "USB enabled: TinyUSB (CDC only)")
endif()

# Link libraries (pico_multicore for multicore_launch_core1; pico_util for queue_t; hardware_adc for pots)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    pico_stdlib
    pico_sync
    pico_multicore
    pico_util
    hardware_dma
    hardware_i2c
    hardware_spi
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
