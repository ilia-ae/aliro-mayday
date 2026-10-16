idf_component_register(SRCS "AccessIntegrations.cpp" INCLUDE_DIRS "include"
    REQUIRES driver esp_timer esp_http_client mbedtls nvs_flash json esp_wifi)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
