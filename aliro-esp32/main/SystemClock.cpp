#include "SystemClock.hpp"
#include <esp_sntp.h>
#include <esp_log.h>

namespace aliro {
void startSystemClock() {
    // Document validity needs wall time even when no OLED panel is enabled.
    // Preserve an existing system time service instead of reinitializing it.
    if (esp_sntp_enabled()) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();
    ESP_LOGI("aliro-clock", "System time sync started for document validity");
}
}
