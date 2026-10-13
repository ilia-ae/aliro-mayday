#pragma once
#include "nvs.h"
constexpr int ESP_LOG_INFO = 3;
inline void esp_log_level_set(const char*, int) {}
inline const char* esp_err_to_name(esp_err_t) { return "test-nvs-error"; }
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
