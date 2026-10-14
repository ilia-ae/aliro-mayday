#include <cstddef>
#include <cstdint>
#include "NfcManagerBridge.hpp"
#include "esp_sntp.h"
void esp_fill_random(void* data,size_t count) {
 static uint8_t sequence=1; auto* bytes=static_cast<uint8_t*>(data);
 for(size_t i=0;i<count;++i) bytes[i]=sequence++;
}
namespace diag {void note_aliro_phase(const char*,const char*) {}}
namespace lockind {void set_locked() {}}
namespace aliro::nfc {void refreshReader(NfcManager*) {} void invalidateReader(NfcManager*) {}}
namespace {bool clock_running=false;unsigned clock_starts=0;}
bool esp_sntp_enabled(){return clock_running;}
void esp_sntp_setoperatingmode(uint8_t){}
void esp_sntp_setservername(uint8_t,const char*){}
void esp_sntp_init(){clock_running=true;++clock_starts;}
namespace test_clock {
void reset(bool running){clock_running=running;clock_starts=0;}
unsigned starts(){return clock_starts;}
}
