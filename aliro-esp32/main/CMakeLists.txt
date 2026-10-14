idf_component_register(
    SRCS
        "app_main.cpp"
        "Diag.cpp"
        "AliroDelegate.cpp"
        "SystemClock.cpp"
        "DoorLockUserStubs.cpp"
        "LockHandlers.cpp"
        # Vendored from rednblkx/HomeKey-ESP32 — see "Reference firmware"
        # in the README. These give us a working PN532 + DDK transaction
        # path; we patch them lightly to (a) drop the HomeSpan event-bus
        # subscription and Arduino-HAL deps we don't have, (b) flip the
        # DigitalKeyType from kHomeKey to kAliro.
        "NfcManager.cpp"
        "ReaderDataManager.cpp"
        "DDKAuthContext.cpp"
        "I2cBus.cpp"
        "RemoteLog.cpp"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        esp_matter
        nvs_flash
        bt
        app_update
        esp_wifi
        esp_netif
        esp_timer
        esp_app_format
        esp_event
        lwip
        pn532_cxx
        pn532_hal
        DigitalDoorKey
        event_bus
        loggable
        loggable_espidf
        # ReaderDataManager (vendored) serializes readerData_t via msgpack-c.
        msgpack-c
        # serialization.hpp (alpaca wrapper) — used by NfcManager event_bus
        # publish payloads. Shipped via component-manager (espp/serialization).
        espp__serialization
        # admin_panel — vendored rednblkx ConfigManager/WebServerManager/
        # MqttManager/WebSocketLogSinker with HomeSpan→Matter shim
        # (MatterAdminGlue) and HK→Aliro identifier rename. Arduino-ESP32
        # is pulled in alongside esp-matter via override_path overrides
        # (see main/idf_component.yml).
        admin_panel
        # mdns — used by app_main to publish hostname `aliro.local` and an
        # `_http._tcp` service so the admin UI is reachable without
        # remembering a DHCP-assigned IP. Matter already pulls mdns in for
        # its own service discovery, but listing it here makes the
        # mdns_hostname_set / mdns_service_add includes resolve cleanly.
        mdns
    # Force --whole-archive on libmain.a so our strong overrides for
    # emberAfPluginDoorLockGetUser/SetUser/GetCredential/SetCredential
    # win over the __attribute__((weak)) defaults compiled into libchip
    # (door-lock-server-callback.cpp). Without this the linker resolves
    # the weak symbol from libchip first (same archive as the call site)
    # and never looks in libmain.
    WHOLE_ARCHIVE
)

# Matter C++ requires C++17 (uses fold expressions, if-constexpr, etc.)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)

# NfcManager.cpp (vendored) uses `%u` for `uint32_t`, which on the xtensa
# toolchain is `long unsigned int` and trips -Werror=format. The upstream
# project doesn't hit this because it pulls Arduino-as-Component which
# sets different default flags. Downgrade format diagnostics from error
# to warning — these are log strings, not memory-safety relevant.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format -Wno-format)

# Optional ByteButton panel driver — only compiled when the user opts in
# via menuconfig (CONFIG_ALIRO_BYTEBUTTON_PANEL=y).
if(CONFIG_ALIRO_BYTEBUTTON_PANEL)
    target_sources(${COMPONENT_LIB} PRIVATE "ByteButtonPanel.cpp")
endif()

# Optional OLED status panel — same Kconfig pattern.
if(CONFIG_ALIRO_OLED_PANEL)
    target_sources(${COMPONENT_LIB} PRIVATE "OledPanel.cpp")
endif()

# BasicInformation::ProductName default — Apple Home pre-fills this string
# in the "Pair Accessory" sheet during commissioning. ConfigurationMgr's
# GetProductName falls back to this define when no factory-data override
# is present. Apple Home iOS truncates at 32 chars per the Matter spec.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME="key.ilia.ae"
    CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME="ilia.ae"
)
