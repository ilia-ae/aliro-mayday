#include "nvs.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace {
using Blobs = std::map<std::string, std::vector<uint8_t>>;
std::map<std::string, Blobs> disk;
struct Handle { std::string space; Blobs pending; };
std::map<nvs_handle_t, Handle> handles;
nvs_handle_t next = 0;
bool failCommit = false;
bool failOpen = false, failSet = false;
std::string commitSpace;
}
void test_nvs::reset() { disk.clear(); handles.clear(); failCommit = failOpen = failSet = false; commitSpace.clear(); }
void test_nvs::failNextCommit() { failCommit = true; commitSpace.clear(); }
void test_nvs::failNextCommitIn(const char* space) { failCommit = true; commitSpace = space; }
void test_nvs::failNextOpen() { failOpen = true; }
void test_nvs::failNextSet() { failSet = true; }
esp_err_t nvs_open(const char* space, int, nvs_handle_t* out) {
    if (failOpen) { failOpen = false; return ESP_FAIL; }
    *out = ++next; handles[*out] = {space, disk[space]}; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { handles.erase(h); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* size) {
    const auto& blob = handles.at(h).pending;
    const auto it = blob.find(key);
    if (it == blob.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) { *size = it->second.size(); return ESP_OK; }
    if (*size < it->second.size()) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = it->second.size(); std::memcpy(out, it->second.data(), *size); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* data, size_t size) {
    if (failSet) { failSet = false; return ESP_FAIL; }
    const auto* p = static_cast<const uint8_t*>(data);
    handles.at(h).pending[key] = {p, p + size}; return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
    return handles.at(h).pending.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    if (failCommit && (commitSpace.empty() || commitSpace == handles.at(h).space)) {
        failCommit = false; commitSpace.clear(); return ESP_FAIL;
    }
    disk[handles.at(h).space] = handles.at(h).pending; return ESP_OK;
}
