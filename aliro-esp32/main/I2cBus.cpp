// Shared I2C0 bus — lazy single-init.
//
// Pulled out of ByteButtonPanel so OLED (and any future PORT.A unit)
// can attach to the same physical bus. Each unit gets its own
// i2c_master_dev_handle_t, but the bus_handle and pin config live here.

#include "I2cBus.hpp"

#include <esp_log.h>

namespace aliro::i2c {

namespace {
constexpr const char * TAG      = "i2c-bus";
constexpr int          kSdaGpio = 8;
constexpr int          kSclGpio = 9;
constexpr int          kI2cPort = 0;

i2c_master_bus_handle_t g_bus = nullptr;
}

i2c_master_bus_handle_t shared_bus()
{
    if (g_bus) return g_bus;

    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port      = kI2cPort;
    cfg.sda_io_num    = static_cast<gpio_num_t>(kSdaGpio);
    cfg.scl_io_num    = static_cast<gpio_num_t>(kSclGpio);
    cfg.clk_source    = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&cfg, &g_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        g_bus = nullptr;
        return nullptr;
    }
    ESP_LOGI(TAG, "shared I2C0 bus up (SDA=%d SCL=%d)", kSdaGpio, kSclGpio);
    return g_bus;
}

}  // namespace aliro::i2c
