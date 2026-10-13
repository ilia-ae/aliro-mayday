idf_component_register(SRC_DIRS          "."
                       INCLUDE_DIRS      "."
                       PRIV_INCLUDE_DIRS  "." "${ESP_MATTER_PATH}/examples/common/utils"
                       PRIV_REQUIRES led_strip nvs_flash esp_wifi esp_event
                                     esp_matter esp_matter_console esp_matter_controller chip
                       LDFRAGMENTS "linker.lf")

if(CONFIG_SPIFFS_ATTESTATION_TRUST_STORE)
    spiffs_create_partition_image(paa_cert ${CMAKE_SOURCE_DIR}/paa_cert FLASH_IN_PROJECT)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
