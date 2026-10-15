#include "NfcManager.hpp"
#include "DDKReaderData.h"
#include "ReaderDataManager.hpp"
// Upstream rednblkx/HomeKey-ESP32 builds against arduino-esp32 component,
// which provides esp32-hal.h (millis, delay, xTaskCreatePinnedToCore, …).
// We're a pure ESP-IDF / esp-matter project — drop the include and below
// patch the two API calls that need it (xTaskCreatePinnedToCore → xTaskCreatePinnedToCore).
#include "eventStructs.hpp"
#include "fmt/ranges.h"
#include "freertos/idf_additions.h"
#include "DDKAuthContext.h"
#include "pn532_hal/spi.hpp"
#include "soc/gpio_num.h"
#include "utils.hpp"

#include <array>
#include <esp_log.h>
#include <chrono>
#include <functional>
#include <serialization.hpp>

#if CONFIG_ALIRO_OLED_PANEL
#include "OledPanel.hpp"
#endif

const char* NfcManager::TAG = "NfcManager";
std::atomic<bool> NfcManager::s_paused{false};

static EventBus::Bus& event_bus = EventBus::Bus::instance();

// LED indicator hooks — defined in LockHandlers.cpp.
//   nfc_reading_start — red fast blink, fires on ANY 14443A detect
//                       (held during SELECT-AID, ~10-15 ms)
//   ddk_auth_start    — cyan fast blink, fires after SELECT-AID
//                       succeeds (HomeKey applet found, DDK auth in
//                       flight, ~100-500 ms)
// The subscriber in app_main.cpp flips to red-1s on reject or
// white→green on accept.
namespace lockind {
    void nfc_reading_start();
    void nfc_reading_failed();
    void ddk_auth_start();
}

/**
 * @brief Task entry wrapper that invokes an instance's auth precompute task.
 *
 * For use as a C-style task entry point; casts the provided task parameter to
 * an NfcManager pointer and calls its authPrecomputeTask method.
 *
 * @param instance Pointer to the NfcManager instance passed as the task parameter.
 */
void NfcManager::authPrecomputeTaskEntry(void* instance) {
  static_cast<NfcManager*>(instance)->authPrecomputeTask();
}

void NfcManager::initAuthPrecompute() {
  if (m_authCtxFreeQueue || m_authCtxReadyQueue || m_authPrecomputeTaskHandle) {
    return;
  }

  m_authCtxFreeQueue = xQueueCreate(kAuthCtxPoolSize, sizeof(AuthCtxCacheItem*));
  m_authCtxReadyQueue = xQueueCreate(kAuthCtxCacheSize, sizeof(AuthCtxCacheItem*));
  if (!m_authCtxFreeQueue || !m_authCtxReadyQueue) {
    ESP_LOGE(TAG, "Failed to create auth precompute queues.");
    if (m_authCtxFreeQueue) {
      vQueueDelete(m_authCtxFreeQueue);
      m_authCtxFreeQueue = nullptr;
    }
    if (m_authCtxReadyQueue) {
      vQueueDelete(m_authCtxReadyQueue);
      m_authCtxReadyQueue = nullptr;
    }
    return;
  }

  for (size_t i = 0; i < kAuthCtxPoolSize; i++) {
    AuthCtxCacheItem* cached = &m_authPool[i];
    m_authPool[i].nfcFn = [this](std::vector<uint8_t>& send, std::vector<uint8_t>& recv, bool isLong) -> bool {
      if (send.size() > 255) {
        return false;
      }
      pn532::Status ok = m_nfc->InDataExchange(send, recv);
      if (ok != pn532::Status::SUCCESS || recv.size() < 4 || recv[0] != 0x41 || recv[1] != 0x00)
          return false;
      recv.erase(recv.begin(), recv.begin() + 2);
      return true;
    };
    m_authPool[i].saveFn = [this, cached](const readerData_t& data) {
      if (!m_readerDataManager.commitAuthentication(data, cached->revision, cached->revision))
        return false;
      // Reader data changed (e.g., new persistent key / endpoint). Drop any cached contexts.
      invalidateAuthCache();
      return true;
    };
    m_authPool[i].savePolicyFn = [this,cached](const readerData_t& data) {
      bool saved=m_readerDataManager.commitDocumentPolicy(data,cached->revision,cached->revision);
      invalidateAuthCache();
      return saved;
    };
    AuthCtxCacheItem* item = &m_authPool[i];
    xQueueSend(m_authCtxFreeQueue, &item, 0);
  }

  // The updated DDK/PSA session constructor overflows the former 4 KiB stack.
  BaseType_t ok = xTaskCreatePinnedToCore(authPrecomputeTaskEntry, "hk_auth_precompute", 8192, this, 3, &m_authPrecomputeTaskHandle, 0);
  if (ok != pdPASS || !m_authPrecomputeTaskHandle) {
    ESP_LOGE(TAG, "Failed to start auth precompute task.");
    m_authPrecomputeTaskHandle = nullptr;
    vQueueDelete(m_authCtxFreeQueue);
    m_authCtxFreeQueue = nullptr;
    vQueueDelete(m_authCtxReadyQueue);
    m_authCtxReadyQueue = nullptr;
    return;
  }

  ESP_LOGI(TAG, "Auth precompute enabled (cache=%u, pool=%u).", kAuthCtxCacheSize, kAuthCtxPoolSize);
}

void NfcManager::invalidateAuthCache() {
  if (!m_hkAuthPrecomputeEnabled) {
    return;
  }
  m_readerDataGeneration.fetch_add(1, std::memory_order_relaxed);

  if (!m_authCtxReadyQueue || !m_authCtxFreeQueue) {
    return;
  }

  AuthCtxCacheItem* item = nullptr;
  uint32_t invalidatedCount = 0;
  while (xQueueReceive(m_authCtxReadyQueue, &item, 0) == pdTRUE) {
    if (!item) continue;
    delete item->ctx;
    item->ctx = nullptr;
    xQueueSend(m_authCtxFreeQueue, &item, 0);
    invalidatedCount++;
  }

  if (invalidatedCount > 0) {
    ESP_LOGI(TAG, "Auth cache invalidated (%u items).", invalidatedCount);
  }
  if (m_authPrecomputeTaskHandle) {
    xTaskNotifyGive(m_authPrecomputeTaskHandle);
  }
}

void NfcManager::authPrecomputeTask() {
  ESP_LOGI(TAG, "Auth precompute task started.");
  while (true) {
    if (!m_authCtxFreeQueue || !m_authCtxReadyQueue) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    const UBaseType_t readyCount = uxQueueMessagesWaiting(m_authCtxReadyQueue);
    if (readyCount >= kAuthCtxCacheSize) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }

    AuthCtxCacheItem* item = nullptr;
    if (xQueueReceive(m_authCtxFreeQueue, &item, pdMS_TO_TICKS(1000)) != pdTRUE || !item) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }

    auto state = m_readerDataManager.getSnapshot();
    readerData_t snapshot = std::move(state.data);
    const bool provisioned =
      // HomeKey reader_gid is 8 B; Aliro reader_group_identifier is 16 B
      // (DoorLock cluster spec). Accept both — kAliro vs kHomeKey is
      // selected by the DDKAuthenticationContext type, not by gid length.
      (snapshot.reader_gid.size() == 8 || snapshot.reader_gid.size() == 16) &&
      !snapshot.reader_id.empty() &&
      !snapshot.reader_sk.empty() &&
      !snapshot.reader_pk.empty();

    if (!provisioned) {
      ESP_LOGD(TAG, "Auth precompute: reader not provisioned yet, retrying...");
      xQueueSend(m_authCtxFreeQueue, &item, 0);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }

    delete item->ctx;
    item->ctx = nullptr;
    item->readerData = std::move(snapshot);
    item->revision = state.revision;
    item->generation = m_readerDataGeneration.load(std::memory_order_relaxed);

    ESP_LOGI(TAG, "Auth precompute: generating (gen=%u, free=%u, ready=%u)...",
             item->generation,
             uxQueueMessagesWaiting(m_authCtxFreeQueue),
             uxQueueMessagesWaiting(m_authCtxReadyQueue));

    auto startTime = std::chrono::high_resolution_clock::now();
    // Use the Aliro flow path. DDK's auth classes are dual-mode (kHomeKey/kAliro)
    // since rednblkx/DigitalDoorKey commit e592f5f3 (feat: experimental Aliro support,
    // 2026-02-11). The upstream NfcManager keeps kHomeKey hard-coded; we flip to
    // kAliro because (a) Apple Wallet on iOS 18+ uses the Aliro path, and
    // (b) Samsung Wallet only speaks Aliro — see README "Testing rig" section.
    item->ctx = new (std::nothrow) DDKAuthenticationContext(kAliro, item->nfcFn, item->readerData, item->saveFn,item->savePolicyFn);
    auto stopTime = std::chrono::high_resolution_clock::now();
    const auto durationMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime).count();

    if (!item->ctx) {
      ESP_LOGE(TAG, "Auth precompute: allocation failed.");
      xQueueSend(m_authCtxFreeQueue, &item, 0);
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    const uint32_t genAfter = m_readerDataGeneration.load(std::memory_order_relaxed);
    if (item->generation != genAfter || item->revision != m_readerDataManager.revision()) {
      ESP_LOGI(TAG, "Auth precompute: stale during generation (itemGen=%u, genNow=%u), retrying...",
               item->generation,
               genAfter);
      delete item->ctx;
      item->ctx = nullptr;
      xQueueSend(m_authCtxFreeQueue, &item, 0);
      continue;
    }

    if (xQueueSend(m_authCtxReadyQueue, &item, 0) != pdTRUE) {
      ESP_LOGW(TAG, "Auth precompute: cache is full unexpectedly, dropping context.");
      delete item->ctx;
      item->ctx = nullptr;
      xQueueSend(m_authCtxFreeQueue, &item, 0);
      continue;
    }

    ESP_LOGI(TAG, "Auth precompute: ready in %lli ms (gen=%u, free=%u, ready=%u)",
             durationMs,
             item->generation,
             uxQueueMessagesWaiting(m_authCtxFreeQueue),
             uxQueueMessagesWaiting(m_authCtxReadyQueue));
  }
}

/**
 * @brief Construct and initialize an NfcManager, set up ECP data and event wiring.
 *
 * Initializes internal state, registers the NFC bus topic, and subscribes to HomeKit/internal
 * events so that ACCESSDATA_CHANGED updates ECP data and invalidates the auth cache, and
 * DEBUG_AUTH_FLOW updates the debug authentication flow when received.
 *
 * @param readerDataManager Reference to the ReaderDataManager used to read and persist reader data.
 * @param nfcGpioPins Four GPIO pin numbers used to construct the PN532 SPI interface.
 * @param hkAuthPrecomputeEnabled If true, enables HomeKit authentication precompute behavior.
 */
NfcManager::NfcManager(ReaderDataManager& readerDataManager, const std::array<uint8_t, 4> &nfcGpioPins, bool hkAuthPrecomputeEnabled)
    : nfcGpioPins(nfcGpioPins),
      m_readerDataManager(readerDataManager),
      m_hkAuthPrecomputeEnabled(hkAuthPrecomputeEnabled),
      m_pollingTaskHandle(nullptr),
      m_retryTaskHandle(nullptr),
      // Apple Enhanced Contactless Polling (ECP) v2 frame for Aliro express
      // mode. Layout (kormax research, github.com/kormax/apple-enhanced-
      // contactless-polling): [6A][ver=02][config=CB][type=02 Access]
      // [subtype=06][TCI=20 42 20 Aliro][reader fingerprint 8B][CRC16 2B].
      // Upstream rednblkx used HomeKey TCI `02 11 00` here — that wakes the
      // HomeKey applet on iPhone Wallet, but the Aliro applet stays dormant
      // (kormax: "Aliro applet is not available when express mode is
      // triggered with the HomeKey TCI 021100"). Without this change DDK
      // FastAuth/StandardAuth on the kAliro path never receives a real
      // Aliro response — every iPhone tap fails auth.
      m_ecpData({ 0x6A, 0x02, 0xCB, 0x02, 0x06, 0x20, 0x42, 0x20 }),
      m_nfc_topic(event_bus.register_topic(NFC_BUS_TOPIC)),
      m_nfc_status_topic(event_bus.register_topic(NFC_STATUS_TOPIC, true))
{
  // Upstream subscribes to HK_BUS_TOPIC (HomeSpan/HomeKit accessory events) here
  // so a controller `accessoryControl` writeback re-emits ECP polling data and
  // invalidates the precomputed auth cache. We don't run HomeSpan — Matter's
  // `SetAliroReaderConfig` is the equivalent provisioning channel, and
  // app_main wires the cache invalidation directly via NfcManager::updateEcpData()
  // when AliroDelegate updates reader keys. Subscription left null.
  m_hk_event = EventBus::SubscriberHandle{};
}

/**
 * @brief Initialize the PN532 SPI interface, create the PN532 instance, and start the NFC polling task.
 *
 * Initializes internal PN532_SPI and PN532 objects and launches the background task that drives NFC polling.
 *
 * @return `true` if the NFC polling task was started, `false` otherwise.
 */
bool NfcManager::begin() {
    m_pn532spi = new pn532::SpiTransport((gpio_num_t)GPIO_NUM_NC, (gpio_num_t)nfcGpioPins[2], (gpio_num_t)nfcGpioPins[3], (gpio_num_t)nfcGpioPins[1], (gpio_num_t)nfcGpioPins[0]); 
    m_nfc = new pn532::Frontend(*m_pn532spi);
    if (m_hkAuthPrecomputeEnabled) {
        initAuthPrecompute();
    } else {
        ESP_LOGI(TAG, "Auth precompute disabled.");
    }
    ESP_LOGI(TAG, "Starting NFC polling task...");
    // Leave room for document verification and policy callbacks on real wallets.
    const auto started = xTaskCreatePinnedToCore(pollingTaskEntry, "nfc_poll_task", 12288, this, 2, &m_pollingTaskHandle, 1);
    if (started != pdPASS || !m_pollingTaskHandle) {
        ESP_LOGE(TAG, "Failed to start NFC polling task.");
        return false;
    }
    return true;
}

/**
 * @brief Update the internal ECP data buffer with the reader GID and its CRC16.
 *
 * If the Reader GID is 8 bytes long, copies it into bytes 8–15 of the internal
 * ECP buffer and computes a CRC16 over the first 16 bytes, storing the 2-byte
 * CRC at bytes 16–17 of the buffer. If the Reader GID is not provisioned,
 * logs a warning and leaves the ECP buffer unchanged.
 */
void NfcManager::updateEcpData() {
    const auto readerData = m_readerDataManager.getReaderDataCopy();
    const auto& readerGid = readerData.reader_gid;
    if (readerGid.size() == 8) {
        // HomeKey: the 8-byte reader GID is the ECP fingerprint directly.
        memcpy(m_ecpData.data() + 8, readerGid.data(), 8);
        Utils::crc16a(m_ecpData.data(), 16, m_ecpData.data() + 16);
        ESP_LOGI(TAG, "ECP data updated for HomeKey (GID-direct, 8B)");
    } else if (readerGid.size() == 16) {
        // Aliro: the 16-byte reader_group_identifier is hashed (SHA-256)
        // and the first 8 bytes of the digest become the ECP fingerprint.
        // This matches the Aliro 1.0 expedited polling spec — Apple Wallet
        // (iOS 18+ HomeKey-as-Aliro path) recognises this fingerprint and
        // wakes its Wallet credential lookup.
        uint8_t digest[32];
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, /*is224=*/0);
        mbedtls_sha256_update(&ctx, readerGid.data(), readerGid.size());
        mbedtls_sha256_finish(&ctx, digest);
        mbedtls_sha256_free(&ctx);
        memcpy(m_ecpData.data() + 8, digest, 8);
        Utils::crc16a(m_ecpData.data(), 16, m_ecpData.data() + 16);
        ESP_LOGI(TAG, "ECP data updated for Aliro (SHA-256(GID)[0:8])");
    } else {
        ESP_LOGW(TAG, "Reader GID is %u bytes — neither HomeKey (8) nor Aliro (16). "
                      "ECP data is invalid; iPhone Wallet won't wake on tap.",
                 (unsigned)readerGid.size());
    }
}

/**
 * @brief Initialize and configure the PN532 NFC reader and refresh ECP data.
 *
 * Attempts to start the PN532, verify firmware presence, configure the SAM,
 * RF field, and passive activation retry policy, then updates the manager's
 * ECP data buffer.
 *
 * @return `true` if the reader was successfully initialized and ECP data updated, `false` if firmware/version check failed and the reader was stopped.
 */
bool NfcManager::initializeReader() {
    m_nfc->begin();
    uint32_t versiondata = m_nfc->GetFirmwareVersion();
    if (!versiondata) {
        ESP_LOGE(TAG, "Error establishing PN532 connection.");
        return false;
    }
    ESP_LOGI(TAG, "Found chip PN532, Firmware ver. %d.%d", (versiondata >> 24) & 0xFF, (versiondata >> 16) & 0xFF);
    EventNfcStatus status{.connected = true, .firmwareVersionMajor = static_cast<uint8_t>((versiondata >> 24) & 0xFF), .firmwareVersionMinor = static_cast<uint8_t>((versiondata >> 16) & 0xFF)};
    std::array<uint8_t, sizeof(EventNfcStatus)> d{};
    size_t d_len = alpaca::serialize(status, d);
    event_bus.publish({m_nfc_status_topic, 0, d.data(), d_len});
    m_nfc->RFConfiguration(0x01, {0x03});
    m_nfc->setPassiveActivationRetries(0);
    m_nfc->RFConfiguration(0x02, {0x00, 0x0B, 0x10});
    m_nfc->RFConfiguration(0x04, {0xFF});
    ESP_LOGI(TAG, "Reader initialized. Waiting for tags...");
    updateEcpData();
#if CONFIG_ALIRO_OLED_PANEL
    aliro::OledPanel::notifyNfcReady();   // boot-splash checklist tick
#endif
    return true;
}

/**
 * @brief Starts the PN532 reconnection retry task if it is not already running.
 *
 * Creates and stores a task handle for the background retry loop; if a retry task
 * is already active, this function returns without side effects.
 */
void NfcManager::startRetryTask() {
    if (m_retryTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(retryTaskEntry, "nfc_retry_task", 4096, this, 5, &m_retryTaskHandle, 1);
    }
}

/**
 * @brief FreeRTOS task entry that dispatches to an instance's polling loop.
 *
 * This static function is used as a task entry point and calls the associated
 * NfcManager instance's pollingTask method.
 *
 * @param instance Pointer to the NfcManager instance whose pollingTask will be executed.
 */
void NfcManager::pollingTaskEntry(void* instance) {
    static_cast<NfcManager*>(instance)->pollingTask();
}

/**
 * @brief RTOS task entry point for the NFC retry task.
 *
 * This function is the C-style entry invoked by the RTOS; it expects a pointer
 * to an NfcManager instance (passed as void*) and transfers control to that
 * instance's retry task implementation.
 *
 * @param instance Pointer to an NfcManager instance, provided by the RTOS task creation call.
 */
void NfcManager::retryTaskEntry(void* instance) {
    static_cast<NfcManager*>(instance)->retryTask();
}

/**
 * @brief Continuously attempts to reconnect to the PN532 and restores normal polling on success.
 *
 * Repeatedly calls reader initialization until it succeeds. When the PN532 is reinitialized,
 * the method resumes the polling task if present, clears the retry task handle, and terminates
 * the current retry task. On failure, it waits 5 seconds before trying again.
 */
void NfcManager::retryTask() {
    ESP_LOGI(TAG, "Starting PN532 reconnection task...");
    while (true) {
        if (initializeReader()) {
            ESP_LOGI(TAG, "PN532 reconnected successfully.");
            if (m_pollingTaskHandle) vTaskResume(m_pollingTaskHandle);
            m_retryTaskHandle = nullptr;
            vTaskDelete(NULL);
        }
        ESP_LOGW(TAG, "Reconnect attempt failed. Retrying in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief Main NFC polling loop that monitors the PN532 reader and dispatches tag handling.
 *
 * @details Initializes the NFC reader and then runs indefinitely, repeatedly
 * checking the reader's responsiveness and polling for passive ISO14443A tags.
 * If the reader becomes unresponsive, this task schedules a reconnection retry
 * and suspends itself. When a tag is detected, it invokes the tag handling path
 * and waits for the tag to be removed before continuing normal polling.
 */
void NfcManager::pollingTask() {
    if (!initializeReader()) {
      startRetryTask();
      vTaskSuspend(NULL);
    }

    while (true) {
        // Idle while WebServerManager has us paused (OTA upload). Without
        // this, ECP polling continues to write to the PN532 over I2C while
        // flash is mid-erase, which sometimes glitches the chip-stack
        // scheduler. setPaused(false) on every OTA exit path.
        if (s_paused.load()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (m_nfc->WriteRegister({0x63,0x3d,0x0}) != pn532::SUCCESS) {
            ESP_LOGE(TAG, "PN532 is unresponsive. Attempting to reconnect...");
            EventNfcStatus status{.connected = false, .firmwareVersionMajor = 0, .firmwareVersionMinor = 0};
            std::array<uint8_t, sizeof(EventNfcStatus)> d{};
            size_t d_len = alpaca::serialize(status, d);
            event_bus.publish({m_nfc_status_topic, 0, d.data(), d_len});
            startRetryTask();
            vTaskSuspend(NULL);
            continue;
        }

        std::vector<uint8_t> res;
        m_nfc->InCommunicateThru(std::vector<uint8_t>(m_ecpData.begin(), m_ecpData.end()), res, 50);

        std::vector<uint8_t> uid;
        std::array<uint8_t,2> sens_res;
        uint8_t sel_res;
        pn532::Status passiveTargetFound = m_nfc->InListPassiveTarget(PN532_MIFARE_ISO14443A, uid, sens_res, sel_res, 500);
        
        if (passiveTargetFound == pn532::SUCCESS) {
            ESP_LOGI(TAG, "NFC tag detected!");
            handleTagPresence(uid, sens_res, sel_res);
            pn532::Status status;
            while ((status = m_nfc->InListPassiveTarget(0x00, uid, sens_res, sel_res)) == pn532::SUCCESS) {
              m_nfc->InRelease(1);
              vTaskDelay(pdMS_TO_TICKS(60));
            }
            ESP_LOGI(TAG, "NFC tag removed!");
            m_nfc->setPassiveActivationRetries(0);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        taskYIELD();
    }
}

/**
 * @brief Handles a detected NFC tag by attempting to select the HomeKey applet and processing the tag accordingly.
 *
 * Attempts to select the HomeKey applet on the tag via an APDU select command; if selection succeeds, proceeds with
 * HomeKey authentication handling. If selection fails, reads the tag's UID/ATQA/SAK and processes it as a generic ISO14443A tag.
 * Logs the tag processing duration and releases the PN532 device state before returning.
 */
void NfcManager::handleTagPresence(const std::vector<uint8_t>& uid, const std::array<uint8_t,2>& atqa, const uint8_t& sak) {
    // LED → red fast-blink for the entire transaction window (SELECT + DDK
    // auth or SELECT + generic-tag handling). Subscriber will flip to
    // red-1s on reject or white→green on accept. Done here (not just inside
    // handleHomeKeyAuth) so the user gets visual feedback even when Flipper
    // / a random MIFARE card is presented and the SELECT-AID step fails.
    lockind::nfc_reading_start();

    auto startTime = std::chrono::high_resolution_clock::now();
    // SELECT AID: try Aliro primary AID first (`A0 00 00 09 09 AC CE 55 01`,
    // 9 B), then fall back to HomeKey AID (`A0 00 00 08 58 01 01`, 7 B) so
    // we still work with older iPhones / Apple Watches that haven't migrated
    // to the Aliro applet. Apple Wallet co-hosts both applets in the same
    // pass — the SELECT-AID we send picks which one responds.
    static constexpr uint8_t kAliroSelectCmd[] = {
        0x00, 0xA4, 0x04, 0x00,
        0x09, 0xA0, 0x00, 0x00, 0x09, 0x09, 0xAC, 0xCE, 0x55, 0x01,
        0x00,
    };
    static constexpr uint8_t kHomeKeySelectCmd[] = {
        0x00, 0xA4, 0x04, 0x00,
        0x07, 0xA0, 0x00, 0x00, 0x08, 0x58, 0x01, 0x01,
        0x00,
    };
    std::vector<uint8_t> response;
    pn532::Status status = m_nfc->InDataExchange(
        std::vector<uint8_t>(std::begin(kAliroSelectCmd), std::end(kAliroSelectCmd)),
        response);
    bool aliroOk = status == pn532::SUCCESS && response.size() >= 2 &&
                   response[response.size() - 2] == 0x90 && response[response.size() - 1] == 0x00;

    if (!aliroOk) {
        ESP_LOGI(TAG, "Aliro AID not selected (status=%d resp=%uB) — falling back to HomeKey AID",
                 (int)status, (unsigned)response.size());
        response.clear();
        status = m_nfc->InDataExchange(
            std::vector<uint8_t>(std::begin(kHomeKeySelectCmd), std::end(kHomeKeySelectCmd)),
            response);
    }

    bool selectOk = status == pn532::SUCCESS && response.size() >= 2 &&
                    response[response.size() - 2] == 0x90 && response[response.size() - 1] == 0x00;

    // Check for success SW1=0x90, SW2=0x00
    if (selectOk) {
        if (response.size() < 4 || response[0] != 0x41 || response[1] != 0x00) {
            lockind::nfc_reading_failed();
            m_nfc->InRelease(1);
            return;
        }
        m_lastSelectResponse.assign(response.begin() + 2, response.end());
        ESP_LOGI(TAG, "%s applet selected successfully.", aliroOk ? "Aliro" : "HomeKey");
#if CONFIG_ALIRO_OLED_PANEL
        aliro::OledPanel::notifyAidSelected();        // T1 — AID round-trip done
#endif

        // Pass the entire FCI to the library’s BER parser and version negotiation.

        // Flip LED red→cyan: user can now tell "this is a real key, doing
        // the crypto", as opposed to a random ISO 14443A tag.
        lockind::ddk_auth_start();
        handleHomeKeyAuth();
    } else {
        ESP_LOGI(TAG, "Not a HomeKey tag, or failed to select applet.");
        ESP_LOGD(TAG, "Passive target UID: %s (%d)", fmt::format("{:02X}", fmt::join(uid, "")).c_str(), uid.size());
        handleGenericTag(uid, atqa, sak);
    }
    
    auto stopTime = std::chrono::high_resolution_clock::now();
    ESP_LOGI(TAG, "Total processing time: %lli ms", std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - startTime).count());
    
    m_nfc->InRelease(1);
}

/**
 * @brief Attempt HomeKey authentication for the currently-present NFC tag.
 *
 * Performs the configured HomeKey authentication flow for the active tag and publishes a HOMEKEY_TAP
 * event describing the outcome. On successful authentication, stored reader data may be updated.
 *
 * If HomeKey precomputation is enabled, a precomputed authentication context may be consumed (when
 * generation matches current reader data); otherwise a fresh ("cold") authentication context is used.
 *
 * Side effects: may update ReaderDataManager, publish a HOMEKEY_TAP event on the NFC bus, notify the
 * auth precompute task, and modify internal auth-cache queues.
 */
void NfcManager::handleHomeKeyAuth() {
    // (LED nfc_reading_start already kicked from handleTagPresence — leave
    // as-is here; it's idempotent.)

    auto publishAuthResult = [this](
        const std::tuple<std::vector<uint8_t>, std::vector<uint8_t>, KeyFlow>& authResult,
        const std::vector<uint8_t>& readerId, uint64_t revision
    ) {
        const auto flow = std::get<2>(authResult);
        if ((flow == kFlowFAST || flow == kFlowSTANDARD || flow == kFlowATTESTATION) &&
            m_readerDataManager.authorizationCurrent(std::get<0>(authResult), std::get<1>(authResult), revision)) {
            ESP_LOGI(TAG, "HomeKey authentication successful!");
            EventHKTap s{.status = true, .issuerId = std::get<0>(authResult), .endpointId = std::get<1>(authResult), .readerId = readerId, .credentialRevision = revision };
            std::vector<uint8_t> d;
            alpaca::serialize(s, d);
            NfcEvent event{.type=HOMEKEY_TAP, .data=d};
            std::vector<uint8_t> event_data;
            alpaca::serialize(event, event_data);
            event_bus.publish({m_nfc_topic, 0, event_data.data(), event_data.size()});
        } else {
            ESP_LOGW(TAG, "HomeKey authentication failed (%s).",
                flow == kFlowFailed ? "protocol/credential rejected" :
                "credential revision or current authorization changed");
#if CONFIG_ALIRO_OLED_PANEL
            aliro::OledPanel::notifyAuthFail();
#endif
            EventHKTap s{.status = false, .issuerId = {}, .endpointId = {}, .readerId = {} };
            std::vector<uint8_t> d;
            alpaca::serialize(s, d);
            NfcEvent event{.type=HOMEKEY_TAP, .data=d};
            std::vector<uint8_t> event_data;
            alpaca::serialize(event, event_data);
            event_bus.publish({m_nfc_topic, 0, event_data.data(), event_data.size()});
        }
    };

    auto authenticateCold = [this, &publishAuthResult]() {
        auto state = m_readerDataManager.getSnapshot();
        readerData_t readerData = std::move(state.data);
        uint64_t revision = state.revision;

        // IMPORTANT: HKAuthenticationContext stores references to std::function objects.
        // Do NOT pass lambdas directly (would bind to temporaries and dangle).
        std::function<bool(std::vector<uint8_t>&, std::vector<uint8_t>&, bool)> nfcFn =
            [this](std::vector<uint8_t>& send, std::vector<uint8_t>& recv, bool isLong) -> bool {
                if (send.size() > 255) {
                    return false;
                }
                pn532::Status ok = m_nfc->InDataExchange(send, recv);
                if (ok != pn532::SUCCESS || recv.size() < 4 || recv[0] != 0x41 || recv[1] != 0x00)
                    return false;
                recv.erase(recv.begin(), recv.begin() + 2);
                return true;
            };
        std::function<bool(const readerData_t&)> saveFn = [this, &revision](const readerData_t& data) {
            if (!m_readerDataManager.commitAuthentication(data, revision, revision)) return false;
            invalidateAuthCache();
            return true;
        };
        DDKAuthenticationContext::Save savePolicyFn=[this,&revision](const readerData_t& data) {
            bool saved=m_readerDataManager.commitDocumentPolicy(data,revision,revision);
            invalidateAuthCache(); return saved;
        };
        DDKAuthenticationContext authCtx(kAliro,nfcFn,readerData,saveFn,savePolicyFn);
        // Pass the FCI Proprietary Template parsed from the SELECT-AID
        // response into the auth context. Required by Aliro §8.3.1.13 —
        // baked into the HKDF salt for session-key derivation.
        authCtx.setSelectResponse(m_lastSelectResponse);
#if CONFIG_ALIRO_OLED_PANEL
        aliro::OledPanel::notifyAuthStart();          // T2 — DDK begin
#endif
        auto authResult = authCtx.authenticate(authFlow);
#if CONFIG_ALIRO_OLED_PANEL
        aliro::OledPanel::notifyAuthDone();           // T3 — DDK done
#endif
        publishAuthResult(authResult, readerData.reader_id, revision);
    };

    if (!m_hkAuthPrecomputeEnabled) {
        authenticateCold();
        return;
    }

    const UBaseType_t readyBefore = m_authCtxReadyQueue ? uxQueueMessagesWaiting(m_authCtxReadyQueue) : 0;
    const UBaseType_t freeBefore = m_authCtxFreeQueue ? uxQueueMessagesWaiting(m_authCtxFreeQueue) : 0;
    const uint32_t genNow = m_readerDataGeneration.load(std::memory_order_relaxed);

    AuthCtxCacheItem* item = nullptr;
    bool gotCached = false;
    if (m_authCtxReadyQueue && xQueueReceive(m_authCtxReadyQueue, &item, 0) == pdTRUE) {
      if (item && item->ctx) {
        gotCached = true;
      } else if (item) {
        ESP_LOGW(TAG, "Auth cache item dequeued without context, returning to free queue.");
        if (m_authCtxFreeQueue) {
          xQueueSend(m_authCtxFreeQueue, &item, 0);
        }
        item = nullptr;
      }
    }

    if (gotCached) {
      const bool genMatch = (item->generation == genNow &&
                             item->revision == m_readerDataManager.revision());
      if (!genMatch) {
        ESP_LOGW(TAG, "Auth cache stale (itemGen=%u, genNow=%u) -> cold init.", item->generation, genNow);
        delete item->ctx;
        item->ctx = nullptr;
        xQueueSend(m_authCtxFreeQueue, &item, 0);
        item = nullptr;
      } else {
        const UBaseType_t readyAfter = uxQueueMessagesWaiting(m_authCtxReadyQueue);
        const UBaseType_t freeAfter = m_authCtxFreeQueue ? uxQueueMessagesWaiting(m_authCtxFreeQueue) : 0;
        ESP_LOGI(TAG, "Auth cache hit (gen=%u, free=%u->%u, ready=%u->%u).",
                 genNow, freeBefore, freeAfter, readyBefore, readyAfter);
#if CONFIG_ALIRO_OLED_PANEL
        aliro::OledPanel::notifyCacheHit();
#endif
        if (m_authPrecomputeTaskHandle) {
          xTaskNotifyGive(m_authPrecomputeTaskHandle);
        }

        // FCI from this transaction's SELECT-AID — required for HKDF salt.
        item->ctx->setSelectResponse(m_lastSelectResponse);
#if CONFIG_ALIRO_OLED_PANEL
        aliro::OledPanel::notifyAuthStart();          // T2 — DDK begin (cache path)
#endif
        auto authResult = item->ctx->authenticate(authFlow);
#if CONFIG_ALIRO_OLED_PANEL
        aliro::OledPanel::notifyAuthDone();           // T3 — DDK done (cache path)
#endif
        const auto readerId = item->readerData.reader_id;
        const auto revision = item->revision;
        delete item->ctx;
        item->ctx = nullptr;
        xQueueSend(m_authCtxFreeQueue, &item, 0);
        publishAuthResult(authResult, readerId, revision);
        return;
      }
    }

    ESP_LOGI(TAG, "Auth cache miss (gen=%u, free=%u, ready=%u) -> cold init.", genNow, freeBefore, readyBefore);
#if CONFIG_ALIRO_OLED_PANEL
    aliro::OledPanel::notifyCacheMiss();
#endif
    if (m_authPrecomputeTaskHandle) {
      xTaskNotifyGive(m_authPrecomputeTaskHandle);
    }
    authenticateCold();
}

/**
 * @brief Publish a TAG_TAP NFC event for a detected non-HomeKey (generic) tag.
 *
 * Constructs an EventTagTap containing the tag UID, ATQA, and SAK, serializes it
 * into an NfcEvent payload, and publishes the serialized event to the NFC event
 * topic.
 *
 * @param uid Pointer to the tag UID bytes.
 * @param uidLen Number of bytes in `uid`.
 * @param atqa Pointer to the 2-byte ATQA value.
 * @param sak Pointer to the 1-byte SAK value.
 */
void NfcManager::handleGenericTag(const std::vector<uint8_t>& uid, const std::array<uint8_t,2>& atqa, const uint8_t& sak) {
    EventTagTap s{.uid = uid, .atqa = atqa, .sak = sak};
    std::vector<uint8_t> d;
    alpaca::serialize(s, d);
    NfcEvent event{.type=TAG_TAP, .data=d};
    std::vector<uint8_t> event_data;
    alpaca::serialize(event, event_data);
    event_bus.publish({m_nfc_topic, 0, event_data.data(), event_data.size()});
}

namespace aliro::nfc {
void refreshReader(NfcManager* manager) {
    if (manager) { manager->updateEcpData(); manager->invalidateAuthCacheExternal(); }
}
void invalidateReader(NfcManager* manager) {
    if (manager) manager->invalidateAuthCacheExternal();
}
}
