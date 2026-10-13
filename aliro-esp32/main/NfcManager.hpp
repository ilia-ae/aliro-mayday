#pragma once
#include "DDKReaderData.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <array>
#include <atomic>
#include <functional>
#include "event_bus.hpp"
#include "pn532_cxx/pn532.hpp"
#include "pn532_hal/spi.hpp"

class LockManager;
class HardwareManager;
class ReaderDataManager;
class MqttManager;
class DDKAuthenticationContext;
namespace espConfig { struct misc_config_t; }

class NfcManager {
public:
    NfcManager(ReaderDataManager& readerDataManager, const std::array<uint8_t, 4> &nfcGpioPins, bool hkAuthPrecomputeEnabled);
    /**
 * @brief Unsubscribes the manager's HomeKey event subscription from the global EventBus.
 *
 * Ensures the subscriber identified by m_hk_event is removed when the NfcManager is destroyed.
 */
~NfcManager() {EventBus::Bus::instance().unsubscribe(m_hk_event);}
    bool begin();
    void updateEcpData();

    // Drop the precomputed DDKAuthenticationContext pool so the next tap
    // rebuilds against fresh reader keys. Made public (was private upstream)
    // so AliroDelegate can call it after Matter `SetAliroReaderConfig`
    // updates the keys — without invalidation the precompute task uses
    // the stale snapshot and the first tap silently fails to auth.
    void invalidateAuthCacheExternal() { invalidateAuthCache(); }

    // Pause/resume the polling loop. Set true to silence ECP polling +
    // SELECT-AID transactions during OTA upload — running them in
    // parallel with esp_ota_write causes I2C bus contention and the
    // occasional task-scheduler glitch when flash cache is mid-disable.
    static void setPaused(bool paused) { s_paused.store(paused); }
    static bool isPaused() { return s_paused.load(); }

private:
    static std::atomic<bool> s_paused;
    // --- Task Management ---
    static void pollingTaskEntry(void* instance);
    void pollingTask();
    static void retryTaskEntry(void* instance);
    void retryTask();
    void startRetryTask();

    // --- HomeKey Auth Cache (precompute) ---
    struct AuthCtxCacheItem {
        readerData_t readerData;
        std::function<bool(std::vector<uint8_t>&, std::vector<uint8_t>&, bool)> nfcFn;
        std::function<bool(const readerData_t&)> saveFn;
    std::function<bool(const readerData_t&)> savePolicyFn;
        DDKAuthenticationContext* ctx = nullptr;
        uint32_t generation = 0;
        uint64_t revision = 0;
    };

    // Bumped 1→2 to keep two precomputed DDK contexts ready at all times.
    // Reason: a cold first tap costs ~1250ms (full inline ECDSA + HKDF +
    // session setup); from a warm cache it's ~226ms. With cacheSize=1 the
    // first tap after a long pause was always cold because the precompute
    // task hadn't yet finished its first generation when the user tapped.
    // cacheSize=2 plus poolSize=3 means: after reader provisioning the
    // task generates two contexts back-to-back (~430ms) so even the very
    // first tap and rapid double-taps both hit the cache.
    // Memory cost: ~2KB extra per pool slot.
    static constexpr size_t kAuthCtxCacheSize = 2;
    static constexpr size_t kAuthCtxPoolSize  = 3;
    static void authPrecomputeTaskEntry(void* instance);
    void authPrecomputeTask();
    void initAuthPrecompute();
    void invalidateAuthCache();

    // --- Core NFC Logic ---
    bool initializeReader();
    void handleTagPresence(const std::vector<uint8_t>& uid, const std::array<uint8_t,2>& atqa, const uint8_t& sak);
    void handleHomeKeyAuth();
    void handleGenericTag(const std::vector<uint8_t>& uid, const std::array<uint8_t,2>& atqa, const uint8_t& sak);
    void waitForTagRemoval();
    
    // --- Member Variables ---
    const std::array<uint8_t, 4> &nfcGpioPins;
    pn532::SpiTransport *m_pn532spi;
    pn532::Frontend *m_nfc;

    ReaderDataManager& m_readerDataManager;
    const bool m_hkAuthPrecomputeEnabled;

    TaskHandle_t m_pollingTaskHandle;
    TaskHandle_t m_retryTaskHandle;
    TaskHandle_t m_authPrecomputeTaskHandle = nullptr;

    QueueHandle_t m_authCtxFreeQueue = nullptr;
    QueueHandle_t m_authCtxReadyQueue = nullptr;
    AuthCtxCacheItem m_authPool[kAuthCtxPoolSize];
    std::atomic<uint32_t> m_readerDataGeneration{0};

    std::array<uint8_t, 18> m_ecpData;

    // FCI Proprietary Template (`A5` TLV value) parsed from the most recent
    // SELECT-AID success. The Aliro spec §8.3.1.13 (Expedited-standard key
    // material generation) bakes this into the HKDF salt — without it, the
    // shared session keys (`SKReader`, `SKDevice`) computed on Reader and
    // User Device diverge, every AUTH1 response decrypt fails GCM auth tag
    // verification (mbedtls error -18). Set in handleTagPresence after each
    // SELECT, consumed in handleHomeKeyAuth via DDKAuthenticationContext::setAliroFCI.
    std::vector<uint8_t> m_lastSelectResponse;

    KeyFlow authFlow = KeyFlow::kFlowFAST;

    static const char* TAG;
    EventBus::SubscriberHandle m_hk_event;
    EventBus::TopicHandle m_nfc_topic;
    EventBus::TopicHandle m_nfc_status_topic;
};
