cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bga_reflow_controller)

# 1. Automatically run Python script to gzip web assets for direct firmware ROM embedding
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_web.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE pack_res
)
if(NOT pack_res EQUAL 0)
    message(WARNING "pack_web.py failed with exit code: ${pack_res}")
endif()

# 2. Custom Target: 'ninja factory_bin' creates a single monolithic 16MB factory image
#    Usage: idf.py build && ninja -C build factory_bin
#    Output: build/factory_full_image.bin  → flash at offset 0x0 (online flasher, esptool)
#    Contains: bootloader + partition-table + ota_data + firmware (including embedded web UI)
add_custom_target(factory_bin
    COMMAND esptool --chip esp32s3 merge-bin
        -o "${CMAKE_BINARY_DIR}/factory_full_image.bin"
        --pad-to-size 16MB
        --flash-mode dio
        --flash-freq 80m
        --flash-size 16MB
        0x0      "${CMAKE_BINARY_DIR}/bootloader/bootloader.bin"
        0x8000   "${CMAKE_BINARY_DIR}/partition_table/partition-table.bin"
        0xe000   "${CMAKE_BINARY_DIR}/ota_data_initial.bin"
        0x10000  "${CMAKE_BINARY_DIR}/bga_reflow_controller.bin"
    COMMENT "Generating factory_full_image.bin (16 MB, flash at offset 0x0)"
    DEPENDS app bootloader
)
