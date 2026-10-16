idf_component_register(
    SRCS
        "src/ConfigManager.cpp"
        "src/ConsoleLogSinker.cpp"
        # HardwareManager: NeoPixel + relay GPIO actions. Uses Arduino-ESP32
        # Pixel driver + digital* APIs — disabled until we either route it
        # through the Arduino dependency we just pulled in (Pixel.h ships
        # with arduino-esp32) or migrate to esp-idf led_strip + driver/gpio.
        # Currently lockind in main/LockHandlers.cpp covers the LED job.
        # "src/HardwareManager.cpp"
        "src/LockManager.cpp"
        "src/MqttManager.cpp"
        "src/WebServerManager.cpp"
        "src/WebSocketLogSinker.cpp"
        "src/MatterAdminGlue.cpp"
    INCLUDE_DIRS
        "include"
        # Lets admin_panel pull eventStructs.hpp from main/ — same EventBus
        # topic constants used by NfcManager + AliroDelegate.
        "${CMAKE_SOURCE_DIR}/main"
    REQUIRES
        access_integrations
        nvs_flash
        esp_http_server
        esp_https_server
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        json
        mqtt
        esp_partition
        app_update
        # Core dump partition reader — handleGetCoredump exposes
        # /api/coredump for post-crash debugging over WiFi.
        espcoredump
        spi_flash
        bootloader_support
        mbedtls
        joltwallet__littlefs
        esp_eth
        # WebServerManager uses sodium/randombytes for the per-session
        # nonce passed to the WebSocket auth bypass.
        libsodium
        event_bus
        loggable
        loggable_espidf
        dns_server
        pn532_hal
        DigitalDoorKey
        esp_matter
        # ConfigManager serializes its config blobs as msgpack into a
        # binary NVS column.
        msgpack-c
        # Header-only espp/serialization — required by eventStructs.hpp.
        espp__serialization
        # Arduino-ESP32 — provides WiFi class, ETH.h, GPIO_IS_VALID_GPIO,
        # the Pixel NeoPixel driver. CONFIG_AUTOSTART_ARDUINO=n keeps our
        # esp-idf app_main as the entry point.
        espressif__arduino-esp32
        # Arduino-ESP32 WiFiGeneric.h pulls network_provisioning/manager.h
        # transitively — that header is in the espressif__network_provisioning
        # component which arduino lists as PRIV_REQUIRES (private). We
        # need it as a public REQUIRE here so admin_panel's compile
        # invocation has the include path.
        espressif__network_provisioning
        # Same shape: WiFi.h includes esp_wifi_remote.h via the legacy path
        # for ESP32-P4 builds. Keep linked even though we don't target P4 —
        # build error otherwise.
        # (added on demand if errors surface)
)

# pull fmt header-only — used by config.hpp
include(FetchContent)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG        12.1.0
)
FetchContent_MakeAvailable(fmt)
target_link_libraries(${COMPONENT_LIB} PUBLIC fmt::fmt-header-only)

# ESP_LOG VERBOSE so DEBUG/VERBOSE actually emit
target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE")

# Suppress -Werror=format on copied files (rednblkx is loose with format strings)
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=format" "-Wno-error=unused-variable" "-Wno-error=unused-but-set-variable")

# ---------------------------------------------------------------------------
# Bundled web UI assets — Svelte project at aliro-esp32/data/, build output
# under aliro-esp32/data/dist/, mounted on the device as /spiffs (LittleFS).
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{CI})
    add_custom_target(webui
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/data
        COMMAND bun install
        COMMAND bun run build
        VERBATIM
    )
endif()
littlefs_create_partition_image(spiffs ${CMAKE_SOURCE_DIR}/data/dist FLASH_IN_PROJECT)
