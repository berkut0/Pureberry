cmake_minimum_required(VERSION 3.13)

# Include pico_sdk_import.cmake from the project root
include(pico_sdk_import.cmake)

project(rp2350_puredata_firmware)

# Initialize the Pico SDK
pico_sdk_init()

# Set C/C++ standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Add executable
add_executable(${PROJECT_NAME}.elf)

# Core firmware source files
target_sources(${PROJECT_NAME}.elf PRIVATE
    src/main.c
)

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
    # Heavy-generated headers will be added here
    # ${HEAVY_SOURCE_DIR}
)

# Link libraries
target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    pico_stdlib
    pico_sync
    hardware_i2s
    hardware_dma
    hardware_pwm
    hardware_clocks
)

# Enable USB output, remove uf2 output
pico_enable_stdio_usb(${PROJECT_NAME}.elf 1)
pico_enable_stdio_uart(${PROJECT_NAME}.elf 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME}.elf)

# Print information
message(STATUS "Pico SDK path: ${PICO_SDK_PATH}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
