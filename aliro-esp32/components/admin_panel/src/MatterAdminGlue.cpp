#include "MatterAdminGlue.hpp"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <app/server/Server.h>
#include <platform/CHIPDeviceConfig.h>
#include <platform/PlatformManager.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>
#include <credentials/FabricTable.h>

#include <memory>

namespace matter_admin {

static const char * TAG = "matter-admin";

bool isPaired()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

void erasePairingsAndReboot()
{
    ESP_LOGW(TAG, "[admin] erasing all Matter fabric pairings + reboot");
    // Matter persists fabrics + ACL in the default `nvs` partition under
    // various namespaces (chip-config, chip-counters, chip-fabric-NN, chip-acl).
    // Easiest correct approach: full erase of the default nvs partition.
    // This also wipes the DoorLock cluster's user/credential store
    // (g_users[]/g_creds[]) which lives in `dl_store` namespace — that's
    // intended: factory-reset clears everything controller-related.
    nvs_flash_deinit();
    nvs_flash_erase();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

void eraseWifiAndReboot()
{
    ESP_LOGW(TAG, "[admin] erasing WiFi credentials + reboot");
    nvs_handle_t nvs;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    // Also clear esp_wifi runtime config so STA mode doesn't auto-rejoin.
    wifi_config_t empty {};
    esp_wifi_set_config(WIFI_IF_STA, &empty);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

bool openCommissioningWindow(uint16_t seconds)
{
    ESP_LOGI(TAG, "[admin] opening Basic Commissioning Window for %u s", seconds);
    // Must run on chip task — the CommissioningWindowManager touches
    // global server state. Schedule onto the chip event loop.
    struct Args { uint16_t seconds; };
    auto * args = new Args{seconds};
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t p) {
            std::unique_ptr<Args> a(reinterpret_cast<Args *>(p));
            CHIP_ERROR err = chip::Server::GetInstance()
                .GetCommissioningWindowManager()
                .OpenBasicCommissioningWindow(
                    chip::System::Clock::Seconds16(a->seconds));
            if (err != CHIP_NO_ERROR) {
                ESP_LOGE(TAG, "[admin] OpenBasicCommissioningWindow failed: %" CHIP_ERROR_FORMAT,
                         err.Format());
            } else {
                ESP_LOGI(TAG, "[admin] commissioning window OPEN — any "
                              "controller can now commission with the original "
                              "setup payload (QR/MT:... or 11-digit manual code)");
            }
        },
        reinterpret_cast<intptr_t>(args));
    return true;
}

bool setWifiCredentials(const std::string & ssid, const std::string & password)
{
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        ESP_LOGE(TAG, "[admin] WiFi credentials invalid (ssid=%u pw=%u)",
                 (unsigned)ssid.size(), (unsigned)password.size());
        return false;
    }
    wifi_config_t cfg {};
    memcpy(cfg.sta.ssid, ssid.c_str(), ssid.size());
    memcpy(cfg.sta.password, password.c_str(), password.size());
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "[admin] esp_wifi_set_config failed");
        return false;
    }
    ESP_LOGI(TAG, "[admin] WiFi credentials saved: ssid='%s'", ssid.c_str());
    return true;
}

bool setAdminWebPin(const std::string & pin)
{
    nvs_handle_t nvs;
    if (nvs_open("aliro_admin", NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }
    nvs_set_str(nvs, "web_pin", pin.c_str());
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "[admin] web admin PIN updated (length=%u)", (unsigned)pin.size());
    return true;
}

SetupPayload getSetupPayload()
{
    SetupPayload out;
    chip::SetupPayload payload;
    uint32_t setupPin = 0;
    uint16_t discriminator = 0;

    auto * provider = chip::DeviceLayer::GetCommissionableDataProvider();
    if (provider != nullptr) {
        provider->GetSetupPasscode(setupPin);
        provider->GetSetupDiscriminator(discriminator);
    }

    payload.setUpPINCode = setupPin;
    payload.discriminator.SetLongValue(discriminator);
    payload.vendorID = CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID;
    payload.productID = CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID;
    payload.rendezvousInformation.SetValue(
        chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    {
        std::string qrBuf;
        chip::QRCodeSetupPayloadGenerator gen(payload);
        if (gen.payloadBase38Representation(qrBuf) == CHIP_NO_ERROR) {
            out.qr = qrBuf;
        }
    }
    {
        std::string manualBuf;
        chip::ManualSetupPayloadGenerator gen(payload);
        if (gen.payloadDecimalStringRepresentation(manualBuf) == CHIP_NO_ERROR) {
            out.manualCode = manualBuf;
        }
    }
    return out;
}

}  // namespace matter_admin
