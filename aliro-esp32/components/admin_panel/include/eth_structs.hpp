#pragma once
//
// eth_structs — runtime Ethernet config types used by ConfigManager and
// WebServerManager (rednblkx). The original file was a thin wrapper over
// Arduino-ESP32's ETH.h, defining `eth_phy_type_t` enum mappings + a
// vendor list of board presets (T-ETH-Lite, Olimex POE, etc.).
//
// In the aliro-esp32 deployment we're WiFi-only — no Ethernet PHY on the
// dev rig and no plan to ship one in the first product. Rather than drag
// the Arduino-ESP32 ETH library + esp_eth + esp_eth_mac_*, we stub out
// the types with the minimum surface that ConfigManager + WebServerManager
// reference (so the JSON config blob has Ethernet fields, the web UI
// shows an empty preset list, and the runtime never tries to bring up a
// PHY because miscConfig.ethernetEnabled defaults to false).
//
// To re-enable Ethernet later: pull Arduino-ESP32's `Ethernet` library
// out of the SELECTIVE_COMPILATION blacklist in sdkconfig.defaults and
// restore the original eth_structs.hpp from rednblkx (commit f2193fd).

#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include "defaults.h"

// Stand-in for Arduino-ESP32's eth_phy_type_t. The actual values don't
// matter (Ethernet is not wired) — we just need the enum to exist for
// struct member declarations.
enum eth_phy_type_stub_t : uint8_t {
    ETH_PHY_NONE_STUB = 0,
};
using eth_phy_type_t  = eth_phy_type_stub_t;

#if CONFIG_ETH_USE_ESP32_EMAC
enum eth_clock_mode_stub_t : uint8_t {
    ETH_CLOCK_GPIO0_IN_STUB = 0,
    ETH_CLOCK_GPIO0_OUT_STUB,
    ETH_CLOCK_GPIO16_OUT_STUB,
    ETH_CLOCK_GPIO17_OUT_STUB,
};
using eth_clock_mode_t = eth_clock_mode_stub_t;
#endif

struct eth_chip_desc_t {
    std::string name;
    bool emac = false;
    eth_phy_type_t phy_type = ETH_PHY_NONE_STUB;
};

struct eth_board_presets_t {
    std::string name;
    eth_chip_desc_t ethChip;
#if CONFIG_ETH_USE_ESP32_EMAC
    struct rmii_conf_t {
        int32_t phy_addr = 1;
        uint8_t pin_mcd = 23;
        uint8_t pin_mdio = 18;
        int8_t  pin_power = -1;
        eth_clock_mode_t pin_rmii_clock = ETH_CLOCK_GPIO0_IN_STUB;
    } rmii_conf;
#endif
    struct spi_conf_t {
        uint8_t spi_freq_mhz = 20;
        uint8_t pin_cs       = PIN_UNSET;
        uint8_t pin_irq      = PIN_UNSET;
        uint8_t pin_rst      = PIN_UNSET;
        uint8_t pin_sck      = PIN_UNSET;
        uint8_t pin_miso     = PIN_UNSET;
        uint8_t pin_mosi     = PIN_UNSET;
    } spi_conf;
};

namespace eth_config_ns {
inline const std::map<eth_phy_type_t, eth_chip_desc_t> supportedChips = {};
inline const std::vector<eth_board_presets_t>          boardPresets   = {};
}  // namespace eth_config_ns
