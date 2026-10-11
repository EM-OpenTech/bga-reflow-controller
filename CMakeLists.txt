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

# Reconfigure automatically if web assets or version.txt change
file(GLOB_RECURSE WEB_ASSETS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS 
    ${WEB_ASSETS_FILES}
    "${CMAKE_CURRENT_SOURCE_DIR}/version.txt"
)

# 2. Release images target (Factory Full Image + OTA Update):
#    Automatically generated on every build ('idf.py build' via the ALL target).
#    Outputs:
#      - build/firmware-v<version>.bin    (16 MB factory image, flash at offset 0x0)
#      - build/ota-update-v<version>.bin  (~2 MB app binary for Web-UI OTA updates)
add_custom_target(factory_bin ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_factory_bin.py"
        --build-dir "${CMAKE_BINARY_DIR}"
        --project-dir "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating release binaries (firmware-v<version>.bin & ota-update-v<version>.bin)..."
    DEPENDS app bootloader
)

