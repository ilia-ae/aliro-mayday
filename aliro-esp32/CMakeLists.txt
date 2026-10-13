cmake_minimum_required(VERSION 3.16)

# esp-matter project. Expects these env vars set before `idf.py build`:
#   IDF_PATH         — ESP-IDF checkout (via `. $IDF_PATH/export.sh`)
#   ESP_MATTER_PATH  — esp-matter checkout (via `. $ESP_MATTER_PATH/export.sh`)
# The esp-matter export script also sets MATTER_SDK_PATH for connectedhomeip.

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR
        "ESP_MATTER_PATH is not set. Source esp-matter/export.sh before building. "
        "See README.md for setup instructions.")
endif()

# esp-matter's export.sh only sets ESP_MATTER_PATH. MATTER_SDK_PATH has to be
# derived per-project (matches what examples/*/CMakeLists.txt do).
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aliro_esp32)
