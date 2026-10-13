#include "ReaderDataManager.hpp"
#include "ReaderCredentialOps.hpp"
#include <map>
#include <nvs_flash.h>
#include <esp_log.h>
#include <algorithm>
#include <ranges>
#include "msgpack.h"

const char* ReaderDataManager::TAG = "ReaderDataManager";
const char* ReaderDataManager::NVS_KEY = "READERDATA";

/**
 * @brief Constructs a ReaderDataManager and initializes internal state.
 *
 * Initializes the manager with no open NVS handle and marks it as not initialized.
 */
namespace {
void packBytes(msgpack_packer* pk,const std::vector<uint8_t>& bytes) {
    msgpack_pack_bin(pk,bytes.size());
    if (!bytes.empty()) msgpack_pack_bin_body(pk,bytes.data(),bytes.size());
}
void packRevocations(msgpack_packer* pk,const ddk::aliro::RevocationState& state) {
    msgpack_pack_map(pk,2);
    msgpack_pack_str_with_body(pk,"iteration",9); msgpack_pack_uint64(pk,state.iteration);
    msgpack_pack_str_with_body(pk,"entries",7); msgpack_pack_array(pk,state.entries.size());
    for (const auto& entry:state.entries) {
        msgpack_pack_map(pk,4);
        msgpack_pack_str_with_body(pk,"hash",4); packBytes(pk,entry.public_key_hash);
        msgpack_pack_str_with_body(pk,"id",2); packBytes(pk,entry.id);
        msgpack_pack_str_with_body(pk,"expiry",6);
        if (entry.expiry) msgpack_pack_uint32(pk,*entry.expiry); else msgpack_pack_nil(pk);
        msgpack_pack_str_with_body(pk,"iteration",9); msgpack_pack_uint64(pk,entry.iteration);
    }
}
bool readMap(msgpack_object obj,std::map<std::string,msgpack_object>& map,size_t count) {
    if (obj.type!=MSGPACK_OBJECT_MAP || obj.via.map.size!=count) return false;
    for (uint32_t i=0;i<obj.via.map.size;i++) {
        auto& pair=obj.via.map.ptr[i];
        if (pair.key.type!=MSGPACK_OBJECT_STR) return false;
        std::string key(pair.key.via.str.ptr,pair.key.via.str.size);
        if (!map.emplace(key,pair.val).second) return false;
    }
    return true;
}
bool unpackRevocations(msgpack_object obj,ddk::aliro::RevocationState& state) {
    std::map<std::string,msgpack_object> map;
    if (!readMap(obj,map,2) || !map.count("iteration") || !map.count("entries") ||
        map["iteration"].type!=MSGPACK_OBJECT_POSITIVE_INTEGER || map["entries"].type!=MSGPACK_OBJECT_ARRAY ||
        map["entries"].via.array.size>64) return false;
    ddk::aliro::RevocationState result; result.iteration=map["iteration"].via.u64;
    for (uint32_t i=0;i<map["entries"].via.array.size;i++) {
        std::map<std::string,msgpack_object> fields;
        if (!readMap(map["entries"].via.array.ptr[i],fields,4) ||
            !fields.count("hash") || !fields.count("id") || !fields.count("expiry") || !fields.count("iteration")) return false;
        ddk::aliro::RevocationEntry entry;
        for (auto key:{"hash","id"}) {
            const auto& value=fields[key];
            if (value.type!=MSGPACK_OBJECT_BIN || value.via.bin.size>(key[0]=='h'?32u:16u)) return false;
            auto& bytes=key[0]=='h'?entry.public_key_hash:entry.id;
            if (value.via.bin.size) bytes.assign(value.via.bin.ptr,value.via.bin.ptr+value.via.bin.size);
        }
        if ((!entry.public_key_hash.empty() && entry.public_key_hash.size()!=32) ||
            (entry.public_key_hash.empty() && entry.id.empty()) || fields["iteration"].type!=MSGPACK_OBJECT_POSITIVE_INTEGER) return false;
        entry.iteration=fields["iteration"].via.u64;
        if (fields["expiry"].type!=MSGPACK_OBJECT_NIL) {
            if (fields["expiry"].type!=MSGPACK_OBJECT_POSITIVE_INTEGER || fields["expiry"].via.u64>UINT32_MAX) return false;
            entry.expiry=static_cast<uint32_t>(fields["expiry"].via.u64);
        }
        result.entries.push_back(std::move(entry));
    }
    state=std::move(result); return true;
}
}

ReaderDataManager::ReaderDataManager() : m_isInitialized(false) {
}

/**
 * @brief Releases resources held by the manager and closes the NVS handle if initialized.
 *
 * If the manager opened the NVS namespace during its lifetime, this destructor closes
 * the associated NVS handle to ensure proper cleanup of system resources.
 */
ReaderDataManager::~ReaderDataManager() {
    if (m_isInitialized) {
        nvs_close(m_nvsHandle);
    }
}

/**
 * @brief Initialize NVS storage for reader data and load any existing data.
 *
 * Opens the "SAVED_DATA" NVS namespace, sets the logger level for this component,
 * marks the manager as initialized, and invokes load() to populate in-memory data.
 *
 * @return true if initialization succeeded and load was triggered, false if opening NVS failed.
 */
bool ReaderDataManager::begin() {
  esp_log_level_set(TAG, ESP_LOG_INFO);
    if (m_isInitialized) {
        ESP_LOGW(TAG, "Already initialized.");
        return true;
    }

    esp_err_t err = nvs_open("SAVED_DATA", NVS_READWRITE, &m_nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return false;
    }

    m_isInitialized = true;
    load();
    return true;
}

/**
 * @brief Accesses the current in-memory reader data.
 *
 * @return const readerData_t& Reference to the stored reader data.
 */
const readerData_t& ReaderDataManager::getReaderData() const {
    return m_readerData;
}

/**
 * @brief Returns a thread-safe snapshot copy of the current in-memory reader data.
 *
 * @return readerData_t Copy of the stored reader data.
 */
readerData_t ReaderDataManager::getReaderDataCopy() const {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    return m_readerData;
}

ReaderDataManager::Snapshot ReaderDataManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    return {m_readerData, m_revision};
}
uint64_t ReaderDataManager::revision() const {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    return m_revision;
}
bool ReaderDataManager::mutateReaderData(const std::function<void(readerData_t&)>& change) {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    change(m_readerData);
    ++m_revision;
    return saveLocked();
}
bool ReaderDataManager::commitAuthentication(const readerData_t& data, uint64_t expectedRevision,
                                            uint64_t& committedRevision) {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    if (m_revision != expectedRevision) return false;
    auto previous = m_readerData;
    m_readerData = data;
    committedRevision = ++m_revision;
    if (saveLocked()) return true;
    // Failed enrollment must not create a usable RAM-only credential.
    m_readerData = std::move(previous);
    return false;
}
bool ReaderDataManager::commitDocumentPolicy(const readerData_t& data,uint64_t expectedRevision,
                                             uint64_t& committedRevision) {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    if (m_revision!=expectedRevision) return false;
    auto previous=m_readerData;
    for (auto& issuer:m_readerData.issuers) {
        auto incoming=std::find_if(data.issuers.begin(),data.issuers.end(),
            [&](const auto& i){return i.issuer_pk==issuer.issuer_pk && i.issuer_id==issuer.issuer_id;});
        if (incoming==data.issuers.end()) continue;
        issuer.revocations=incoming->revocations;
        issuer.document_policy_valid=incoming->document_policy_valid;
        issuer.access_iteration=incoming->access_iteration;
        for (auto& endpoint:issuer.endpoints) {
            auto ep=std::find_if(incoming->endpoints.begin(),incoming->endpoints.end(),
                [&](const auto& e){return e.endpoint_pk==endpoint.endpoint_pk;});
            if (ep==incoming->endpoints.end()) continue;
            endpoint.document_bound=ep->document_bound; endpoint.access_document=ep->access_document;
            endpoint.access_iteration=ep->access_iteration;
            if (ep->endpoint_prst_k.empty()) endpoint.endpoint_prst_k.clear();
        }
    }
    committedRevision=++m_revision;
    if (saveLocked()) return true;
    // Policy failure must not undo a new denial or apply an unpersisted grant.
    // Keep the union of revocations and block each affected issuer until a
    // successfully stored, verified policy document recovers it.
    for (auto& issuer:m_readerData.issuers) {
        auto old=std::find_if(previous.issuers.begin(),previous.issuers.end(),
            [&](const auto& i){return i.issuer_pk==issuer.issuer_pk && i.issuer_id==issuer.issuer_id;});
        if (old==previous.issuers.end()) continue;
        issuer.revocations.iteration=old->revocations.iteration;
        for (const auto& entry:old->revocations.entries)
            if (std::find(issuer.revocations.entries.begin(),issuer.revocations.entries.end(),entry)==issuer.revocations.entries.end())
                issuer.revocations.entries.push_back(entry);
        issuer.document_policy_valid=false;
        for (auto& endpoint:issuer.endpoints) endpoint.endpoint_prst_k.clear();
    }
    return false;
}
bool ReaderDataManager::authorizationCurrent(const std::vector<uint8_t>& issuerId,
    const std::vector<uint8_t>& endpointId, uint64_t expectedRevision,std::optional<int64_t> currentTime) const {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    return m_revision == expectedRevision &&
        aliro::credentials::find(m_readerData, issuerId, endpointId) != nullptr &&
        aliro::documents::allowed(m_readerData,issuerId,endpointId,currentTime);
}

/**
 * @brief Accesses the reader's group identifier.
 *
 * @return const std::vector<uint8_t>& Reference to the stored group identifier bytes.
 */
const std::vector<uint8_t>& ReaderDataManager::getReaderGid() const {
    return m_readerData.reader_gid;
}

/**
 * @brief Provides access to the stored reader unique identifier.
 *
 * @return const std::vector<uint8_t>& Reference to the reader unique identifier as a byte vector.
 */
const std::vector<uint8_t>& ReaderDataManager::getReaderId() const {
    return m_readerData.reader_id;
}

/**
 * @brief Loads reader data from NVS into the in-memory reader data structure.
 *
 * Reads the MessagePack-serialized blob stored under the NVS key and deserializes it into
 * m_readerData. If the key is not found, the in-memory reader data is reset to defaults.
 * If the stored blob is empty or cannot be read or parsed, the in-memory state is left
 * unchanged (or reset only when key is not found) and an error is logged.
 *
 * Preconditions:
 * - The NVS namespace must be opened (m_isInitialized == true).
 */
void ReaderDataManager::load() {
    if (!m_isInitialized) {
        ESP_LOGE(TAG, "Cannot load, not initialized.");
        return;
    }

    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(m_nvsHandle, NVS_KEY, NULL, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Reader data not found in NVS. Starting with a clean slate.");
        std::lock_guard<std::mutex> lock(m_readerDataMutex);
        m_readerData = {}; // Reset to default
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) getting blob size for key '%s'", esp_err_to_name(err), NVS_KEY);
        return;
    }
    if (required_size == 0) {
        ESP_LOGW(TAG, "Key '%s' found but size is 0. Using defaults.", NVS_KEY);
        return;
    }

    std::vector<uint8_t> buffer(required_size);
    err = nvs_get_blob(m_nvsHandle, NVS_KEY, buffer.data(), &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) reading blob for key '%s'", esp_err_to_name(err), NVS_KEY);
        return;
    }
    
    msgpack_unpacked unpacked;
    msgpack_unpacked_init(&unpacked);
    bool success = msgpack_unpack_next(&unpacked, (const char*)buffer.data(), buffer.size(), NULL);
    if(success) {
        msgpack_object obj = unpacked.data;
        readerData_t loadedReaderData{};
        unpack_readerData_t(obj, loadedReaderData);
        std::lock_guard<std::mutex> lock(m_readerDataMutex);
        m_readerData = std::move(loadedReaderData);
    } else {
        ESP_LOGE(TAG, "Failed to parse msgpack for reader data. Data may be corrupt.");
    }
    msgpack_unpacked_destroy(&unpacked);
}

/**
 * @brief Persist the in-memory reader data to non-volatile storage (NVS).
 *
 * Serializes the current in-memory readerData_t and writes it to NVS under the class's configured key, then commits the change.
 *
 * @return const readerData_t* Pointer to the current in-memory reader data on success, or `nullptr` on failure (for example, if the manager is not initialized or an NVS error occurs).
 *
 * @note This function persists a snapshot of the in-memory data to NVS. The returned pointer refers to the manager's
 *       current in-memory state, which may change concurrently. If you need a consistent snapshot, call
 *       getReaderDataCopy().
 */
const readerData_t* ReaderDataManager::saveData() {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    return saveLocked() ? &m_readerData : nullptr;
}

bool ReaderDataManager::saveLocked() {
    if (!m_isInitialized) {
        ESP_LOGE(TAG, "Cannot save, not initialized.");
        return false;
    }
    const readerData_t& readerDataSnapshot = m_readerData;
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
    pack_readerData_t(&pk, readerDataSnapshot);

    esp_err_t set_err = nvs_set_blob(m_nvsHandle, NVS_KEY, sbuf.data, sbuf.size);
    msgpack_sbuffer_destroy(&sbuf);

    if (set_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set blob in NVS: %s", esp_err_to_name(set_err));
        return false;
    }

    esp_err_t commit_err = nvs_commit(m_nvsHandle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(commit_err));
        return false;
    }

    ESP_LOGI(TAG, "Reader data successfully saved to NVS.");
    return true;
}

/**
 * @brief Replace the in-memory reader data with the supplied data and persist it to NVS.
 *
 * @param newData The reader data to store (replaces the current in-memory state).
 * @return const readerData_t* Pointer to the stored reader data after a successful save, or `nullptr` on error.
 */
const readerData_t* ReaderDataManager::updateReaderData(const readerData_t& newData) {
    return mutateReaderData([&](readerData_t& data) { data = newData; }) ? &m_readerData : nullptr;
}

/**
 * @brief Clears the in-memory reader key material and persists the cleared state to NVS.
 *
 * If the manager is not initialized, no changes are made and the function returns immediately.
 *
 * @return `true` if the in-memory key was cleared (and a save attempt was issued), `false` if the manager was not initialized.
 */
bool ReaderDataManager::eraseReaderKey() {
    if (!m_isInitialized) {
        ESP_LOGE(TAG, "Cannot delete, not initialized.");
        return false;
    }
    
    return mutateReaderData([](readerData_t& data) {
        data.reader_gid.clear(); data.reader_id.clear(); data.reader_pk.clear();
        data.reader_pk_x.clear(); data.reader_sk.clear();
    });
}

/**
 * @brief Erase all reader data from both in-memory state and NVS persistent storage.
 *
 * @details Clears the in-memory readerData_t, removes the stored blob under the manager's NVS key,
 * and commits the erase to NVS. Fails if the manager is not initialized or if erase/commit operations fail.
 *
 * @return true if the in-memory data was cleared and the NVS erase + commit succeeded, false otherwise.
 */
bool ReaderDataManager::deleteAllReaderData() {
    if (!m_isInitialized) {
        ESP_LOGE(TAG, "Cannot delete, not initialized.");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    m_readerData = {};
    ++m_revision;
    ESP_LOGI(TAG, "In-memory reader data cleared.");

    esp_err_t erase_err = nvs_erase_key(m_nvsHandle, NVS_KEY);
    if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase NVS key '%s': %s", NVS_KEY, esp_err_to_name(erase_err));
        return false;
    }

    esp_err_t commit_err = nvs_commit(m_nvsHandle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS erase: %s", esp_err_to_name(commit_err));
        return false;
    }
    
    ESP_LOGI(TAG, "Reader data successfully erased from NVS.");
    return true;
}

/**
 * @brief Adds a new issuer to the in-memory issuer list if an issuer with the same identifier is not already present.
 *
 * If no existing issuer has an identifier equal to `issuerId`, appends a new issuer to the manager's in-memory
 * issuers vector using `issuerId` and the first 32 bytes of `publicKey`.
 *
 * @param issuerId Byte sequence identifying the issuer.
 * @param publicKey Pointer to the issuer's public key bytes; the first 32 bytes are copied.
 * @return true if a new issuer was appended, false if an issuer with the same identifier already existed.
 */
bool ReaderDataManager::addIssuerIfNotExists(const std::vector<uint8_t>& issuerId, const uint8_t* publicKey) {
    std::lock_guard<std::mutex> lock(m_readerDataMutex);
    for (const auto& issuer : m_readerData.issuers) {
        if (issuer.issuer_id.size() == issuerId.size() && 
            std::equal(issuer.issuer_id.begin(), issuer.issuer_id.end(), issuerId.begin())) {
            ESP_LOGD(TAG, "Issuer already exists, skipping.");
            return false;
        }
    }

    ESP_LOGI(TAG, "Adding new issuer.");
    hkIssuer_t newIssuer;
    newIssuer.issuer_id = issuerId;
    newIssuer.issuer_pk.assign(publicKey, publicKey + 32);

    m_readerData.issuers.emplace_back(newIssuer);
    ++m_revision;
    return true;
}

/**
 * @brief Packs an hkEndpoint_t into MessagePack format using the provided packer.
 *
 * Serializes the endpoint's seven fields: "endpointId", "last_used_at", "counter",
 * "key_type", "publicKey", "endpoint_key_x", and "persistent_key".
 *
 * @param pk MessagePack packer used to write the serialized data.
 * @param endpoint Endpoint structure whose contents will be serialized.
 */

void ReaderDataManager::pack_hkEndpoint_t(msgpack_packer* pk, const hkEndpoint_t& endpoint) {
    msgpack_pack_map(pk, 12);
    msgpack_pack_str_with_body(pk,"document_bound",14); if (endpoint.document_bound) msgpack_pack_true(pk); else msgpack_pack_false(pk);
    msgpack_pack_str_with_body(pk,"access_document",15);
    msgpack_pack_bin(pk,endpoint.access_document.size());
    if (!endpoint.access_document.empty()) msgpack_pack_bin_body(pk,endpoint.access_document.data(),endpoint.access_document.size());
    msgpack_pack_str_with_body(pk, "access_iteration", 16);
    msgpack_pack_uint64(pk, endpoint.access_iteration);
    msgpack_pack_str_with_body(pk, "credential_index", 16);
    msgpack_pack_uint16(pk, endpoint.credential_index);
    msgpack_pack_str_with_body(pk, "credential_type", 15);
    msgpack_pack_uint8(pk, endpoint.credential_type);

    msgpack_pack_str(pk, strlen("endpointId"));
    msgpack_pack_str_body(pk, "endpointId", strlen("endpointId"));
    msgpack_pack_array(pk, endpoint.endpoint_id.size());
    std::ranges::for_each(endpoint.endpoint_id, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });

    msgpack_pack_str(pk, strlen("last_used_at"));
    msgpack_pack_str_body(pk, "last_used_at", strlen("last_used_at"));
    msgpack_pack_unsigned_int(pk, endpoint.last_used_at);

    msgpack_pack_str(pk, strlen("counter"));
    msgpack_pack_str_body(pk, "counter", strlen("counter"));
    msgpack_pack_int(pk, endpoint.counter);

    msgpack_pack_str(pk, strlen("key_type"));
    msgpack_pack_str_body(pk, "key_type", strlen("key_type"));
    msgpack_pack_int(pk, endpoint.key_type);

    msgpack_pack_str(pk, strlen("publicKey"));
    msgpack_pack_str_body(pk, "publicKey", strlen("publicKey"));
    msgpack_pack_array(pk, endpoint.endpoint_pk.size());
    std::ranges::for_each(endpoint.endpoint_pk, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });

    msgpack_pack_str(pk, strlen("endpoint_key_x"));
    msgpack_pack_str_body(pk, "endpoint_key_x", strlen("endpoint_key_x"));
    msgpack_pack_array(pk, endpoint.endpoint_pk_x.size());
    std::ranges::for_each(endpoint.endpoint_pk_x, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });

    msgpack_pack_str(pk, strlen("persistent_key"));
    msgpack_pack_str_body(pk, "persistent_key", strlen("persistent_key"));
    msgpack_pack_array(pk, endpoint.endpoint_prst_k.size());
    std::ranges::for_each(endpoint.endpoint_prst_k, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });
}

/**
 * @brief Deserialize a MessagePack object into an hkEndpoint_t structure.
 *
 * Parses a MessagePack map containing endpoint fields and populates the provided
 * hkEndpoint_t with any present entries. Recognized keys (and their target
 * members) are: "endpointId" -> endpoint_id, "last_used_at" -> last_used_at,
 * "counter" -> counter, "key_type" -> key_type, "publicKey" -> endpoint_pk,
 * "endpoint_key_x" -> endpoint_pk_x, and "persistent_key" -> endpoint_prst_k.
 *
 * If the incoming object is not a map, the function returns immediately and
 * does not modify the output parameter.
 *
 * @param obj MessagePack object expected to be a map representing an endpoint.
 * @param[out] endpoint Destination hkEndpoint_t to populate with parsed values.
 */
void ReaderDataManager::unpack_hkEndpoint_t(msgpack_object obj, hkEndpoint_t& endpoint) {
    if (obj.type != MSGPACK_OBJECT_MAP) {
        ESP_LOGE(TAG, "Error: Expected map for hkEndpoint_t deserialization.");
        return;
    }

    std::map<std::string, msgpack_object> obj_map;
    for (uint32_t i = 0; i < obj.via.map.size; ++i) {
        msgpack_object_kv* kv = &obj.via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR) {
            obj_map[std::string(kv->key.via.str.ptr, kv->key.via.str.size)] = kv->val;
        }
    }

    if (auto found=obj_map.find("document_bound"); found!=obj_map.end())
        endpoint.document_bound=found->second.type!=MSGPACK_OBJECT_BOOLEAN || found->second.via.boolean;
    if (auto found=obj_map.find("access_document"); found!=obj_map.end()) {
        const auto& value=found->second;
        if (value.type==MSGPACK_OBJECT_BIN && value.via.bin.size<=4096) {
            endpoint.access_document.assign(value.via.bin.ptr,value.via.bin.ptr+value.via.bin.size);
            if (!endpoint.access_document.empty()) endpoint.document_bound=true;
        } else endpoint.document_bound=true;
    }
    if (obj_map.count("endpointId") && obj_map["endpointId"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["endpointId"].via.array.ptr, obj_map["endpointId"].via.array.ptr + obj_map["endpointId"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      endpoint.endpoint_id.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("last_used_at") && obj_map["last_used_at"].type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
      endpoint.last_used_at = obj_map["last_used_at"].via.u64;
    }
    if (obj_map.count("access_iteration") && obj_map["access_iteration"].type == MSGPACK_OBJECT_POSITIVE_INTEGER)
        endpoint.access_iteration = obj_map["access_iteration"].via.u64;
    if (obj_map.count("credential_index") && obj_map["credential_index"].type == MSGPACK_OBJECT_POSITIVE_INTEGER)
        endpoint.credential_index = obj_map["credential_index"].via.u64;
    if (obj_map.count("credential_type") && obj_map["credential_type"].type == MSGPACK_OBJECT_POSITIVE_INTEGER)
        endpoint.credential_type = obj_map["credential_type"].via.u64;
    if (obj_map.count("counter") && obj_map["counter"].type == MSGPACK_OBJECT_POSITIVE_INTEGER) { // Can be negative, but for simplicity, assuming positive
      endpoint.counter = obj_map["counter"].via.i64;
    }
    if (obj_map.count("key_type") && obj_map["key_type"].type == MSGPACK_OBJECT_POSITIVE_INTEGER) { // Can be negative, but for simplicity, assuming positive
      endpoint.key_type = obj_map["key_type"].via.i64;
    }
    if (obj_map.count("publicKey") && obj_map["publicKey"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["publicKey"].via.array.ptr, obj_map["publicKey"].via.array.ptr + obj_map["publicKey"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      endpoint.endpoint_pk.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("endpoint_key_x") && obj_map["endpoint_key_x"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["endpoint_key_x"].via.array.ptr, obj_map["endpoint_key_x"].via.array.ptr + obj_map["endpoint_key_x"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      endpoint.endpoint_pk_x.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("persistent_key") && obj_map["persistent_key"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["persistent_key"].via.array.ptr, obj_map["persistent_key"].via.array.ptr + obj_map["persistent_key"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      endpoint.endpoint_prst_k.assign(integer_view.begin(), integer_view.end());
    }
}

/**
 * @brief Serializes an hkIssuer_t into MessagePack format and writes it to the given packer.
 *
 * The issuer is encoded as a map with four keys: "issuerId", "publicKey", "issuer_key_x", and "endpoints".
 * Byte-vector fields are written as arrays of unsigned bytes. The "endpoints" key is written as an array
 * of endpoint objects, each serialized as a nested MessagePack structure.
 *
 * @param pk Pointer to an active msgpack_packer used for writing the serialized data.
 * @param issuer The issuer object whose fields will be serialized into the packer.
 */
void ReaderDataManager::pack_hkIssuer_t(msgpack_packer* pk, const hkIssuer_t& issuer) {
    msgpack_pack_map(pk, 8);
    msgpack_pack_str_with_body(pk,"document_policy_valid",21);
    if (issuer.document_policy_valid) msgpack_pack_true(pk); else msgpack_pack_false(pk);
    msgpack_pack_str_with_body(pk,"revocations",11); packRevocations(pk,issuer.revocations);
    msgpack_pack_str_with_body(pk, "access_iteration", 16);
    msgpack_pack_uint64(pk, issuer.access_iteration);
    msgpack_pack_str_with_body(pk, "credential_index", 16);
    msgpack_pack_uint16(pk, issuer.credential_index);

    msgpack_pack_str(pk, strlen("issuerId"));
    msgpack_pack_str_body(pk, "issuerId", strlen("issuerId"));
    msgpack_pack_array(pk, issuer.issuer_id.size());
    std::ranges::for_each(issuer.issuer_id, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("publicKey"));
    msgpack_pack_str_body(pk, "publicKey", strlen("publicKey"));
    msgpack_pack_array(pk, issuer.issuer_pk.size());
    std::ranges::for_each(issuer.issuer_pk, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("issuer_key_x"));
    msgpack_pack_str_body(pk, "issuer_key_x", strlen("issuer_key_x"));
    msgpack_pack_array(pk, issuer.issuer_pk_x.size());
    std::ranges::for_each(issuer.issuer_pk_x, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("endpoints"));
    msgpack_pack_str_body(pk, "endpoints", strlen("endpoints"));
    msgpack_pack_array(pk, issuer.endpoints.size());
    for (const auto& endpoint : issuer.endpoints) {
        pack_hkEndpoint_t(pk, endpoint);
    }
}

/**
 * @brief Deserialize an hkIssuer_t from a MessagePack map.
 *
 * Parses the provided MessagePack object and populates the given `issuer`
 * with any present fields: `issuerId`, `publicKey`, `issuer_key_x`, and
 * `endpoints`. Array fields are converted to their corresponding byte
 * vectors; `endpoints` is deserialized into `issuer.endpoints` using
 * `unpack_hkEndpoint_t`. Fields that are not present are left unchanged.
 *
 * If `obj` is not a map, the function logs an error and returns without
 * modifying `issuer`.
 *
 * @param obj MessagePack object expected to be a map representing an hkIssuer_t.
 * @param[out] issuer Reference to the hkIssuer_t to populate.
 */
void ReaderDataManager::unpack_hkIssuer_t(msgpack_object obj, hkIssuer_t& issuer) {
    if (obj.type != MSGPACK_OBJECT_MAP) {
        ESP_LOGE(TAG, "Error: Expected map for hkIssuer_t deserialization.");
        return;
    }

    std::map<std::string, msgpack_object> obj_map;
    for (uint32_t i = 0; i < obj.via.map.size; ++i) {
        msgpack_object_kv* kv = &obj.via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR) {
            obj_map[std::string(kv->key.via.str.ptr, kv->key.via.str.size)] = kv->val;
        }
    }

    if (auto found=obj_map.find("document_policy_valid"); found!=obj_map.end())
        issuer.document_policy_valid=found->second.type==MSGPACK_OBJECT_BOOLEAN && found->second.via.boolean;
    if (auto found=obj_map.find("revocations"); found!=obj_map.end())
        if (!unpackRevocations(found->second,issuer.revocations)) issuer.document_policy_valid=false;
    if (obj_map.count("issuerId") && obj_map["issuerId"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["issuerId"].via.array.ptr, obj_map["issuerId"].via.array.ptr + obj_map["issuerId"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      issuer.issuer_id.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("access_iteration") && obj_map["access_iteration"].type == MSGPACK_OBJECT_POSITIVE_INTEGER)
        issuer.access_iteration = obj_map["access_iteration"].via.u64;
    if (obj_map.count("credential_index") && obj_map["credential_index"].type == MSGPACK_OBJECT_POSITIVE_INTEGER)
        issuer.credential_index = obj_map["credential_index"].via.u64;
    if (obj_map.count("publicKey") && obj_map["publicKey"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["publicKey"].via.array.ptr, obj_map["publicKey"].via.array.ptr + obj_map["publicKey"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      issuer.issuer_pk.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("issuer_key_x") && obj_map["issuer_key_x"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["issuer_key_x"].via.array.ptr, obj_map["issuer_key_x"].via.array.ptr + obj_map["issuer_key_x"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      issuer.issuer_pk_x.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("endpoints") && obj_map["endpoints"].type == MSGPACK_OBJECT_ARRAY) {
        msgpack_object_array endpoints_array = obj_map["endpoints"].via.array;
        issuer.endpoints.resize(endpoints_array.size);
        for (uint32_t i = 0; i < endpoints_array.size; ++i) {
            unpack_hkEndpoint_t(endpoints_array.ptr[i], issuer.endpoints[i]);
        }
    }
}

/**
 * @brief Serializes a readerData_t instance into MessagePack and appends it to the packer.
 *
 * Packs a map with the keys `reader_private_key`, `reader_public_key`, `reader_key_x`,
 * `group_identifier`, `unique_identifier`, and `issuers`. Byte-array fields are written
 * as arrays of unsigned bytes; `issuers` is written as an array with each entry serialized
 * via pack_hkIssuer_t.
 *
 * @param pk Pointer to an initialized msgpack_packer that will receive the serialized data.
 * @param reader_data The readerData_t object to serialize.
 */
void ReaderDataManager::pack_readerData_t(msgpack_packer* pk, const readerData_t& reader_data) {
    msgpack_pack_map(pk, 7);
    msgpack_pack_str_with_body(pk, "revoked_endpoint_keys", 21);
    msgpack_pack_array(pk, reader_data.revoked_endpoint_keys.size());
    for (const auto& key : reader_data.revoked_endpoint_keys) {
        msgpack_pack_array(pk, key.size());
        for (uint8_t byte : key) msgpack_pack_uint8(pk, byte);
    }

    msgpack_pack_str(pk, strlen("reader_private_key"));
    msgpack_pack_str_body(pk, "reader_private_key", strlen("reader_private_key"));
    msgpack_pack_array(pk, reader_data.reader_sk.size());
    std::ranges::for_each(reader_data.reader_sk, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("reader_public_key"));
    msgpack_pack_str_body(pk, "reader_public_key", strlen("reader_public_key"));
    msgpack_pack_array(pk, reader_data.reader_pk.size());
    std::ranges::for_each(reader_data.reader_pk, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("reader_key_x"));
    msgpack_pack_str_body(pk, "reader_key_x", strlen("reader_key_x"));
    msgpack_pack_array(pk, reader_data.reader_pk_x.size());
    std::ranges::for_each(reader_data.reader_pk_x, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("group_identifier"));
    msgpack_pack_str_body(pk, "group_identifier", strlen("group_identifier"));
    msgpack_pack_array(pk, reader_data.reader_gid.size());
    std::ranges::for_each(reader_data.reader_gid, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("unique_identifier"));
    msgpack_pack_str_body(pk, "unique_identifier", strlen("unique_identifier"));
    msgpack_pack_array(pk, reader_data.reader_id.size());
    std::ranges::for_each(reader_data.reader_id, [&pk](const auto&o){
      msgpack_pack_unsigned_char(pk, o);
    });  

    msgpack_pack_str(pk, strlen("issuers"));
    msgpack_pack_str_body(pk, "issuers", strlen("issuers"));
    msgpack_pack_array(pk, reader_data.issuers.size());
    for (const auto& issuer : reader_data.issuers) {
        pack_hkIssuer_t(pk, issuer);
    }
}

/**
 * @brief Populate a readerData_t structure from a MessagePack object.
 *
 * Deserializes expected fields from a MessagePack map into the provided reader_data structure; missing or type-mismatched fields are left unchanged and the function returns immediately if the top-level object is not a map.
 *
 * @param obj MessagePack object expected to be a map containing keys: "reader_private_key", "reader_public_key", "reader_key_x", "group_identifier", "unique_identifier", and "issuers".
 * @param[out] reader_data Destination structure that will be updated with any fields present and correctly typed in the MessagePack map.
 */
void ReaderDataManager::unpack_readerData_t(msgpack_object obj, readerData_t& reader_data) {
    if (obj.type != MSGPACK_OBJECT_MAP) {
        ESP_LOGE(TAG, "Error: Expected map for readerData_t deserialization.");
        return;
    }

    std::map<std::string, msgpack_object> obj_map;
    for (uint32_t i = 0; i < obj.via.map.size; ++i) {
        msgpack_object_kv* kv = &obj.via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR) {
            obj_map[std::string(kv->key.via.str.ptr, kv->key.via.str.size)] = kv->val;
        }
    } 

    if (obj_map.count("reader_private_key") && obj_map["reader_private_key"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["reader_private_key"].via.array.ptr, obj_map["reader_private_key"].via.array.ptr + obj_map["reader_private_key"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      reader_data.reader_sk.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("reader_public_key") && obj_map["reader_public_key"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["reader_public_key"].via.array.ptr, obj_map["reader_public_key"].via.array.ptr + obj_map["reader_public_key"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      reader_data.reader_pk.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("reader_key_x") && obj_map["reader_key_x"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["reader_key_x"].via.array.ptr, obj_map["reader_key_x"].via.array.ptr + obj_map["reader_key_x"].via.array.size);
    auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      reader_data.reader_pk_x.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("group_identifier") && obj_map["group_identifier"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["group_identifier"].via.array.ptr, obj_map["group_identifier"].via.array.ptr + obj_map["group_identifier"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      reader_data.reader_gid.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("unique_identifier") && obj_map["unique_identifier"].type == MSGPACK_OBJECT_ARRAY) {
      auto msgpack_elements = std::ranges::subrange(obj_map["unique_identifier"].via.array.ptr, obj_map["unique_identifier"].via.array.ptr + obj_map["unique_identifier"].via.array.size);
      auto integer_view = msgpack_elements | std::ranges::views::transform([](const msgpack_object& o){return o.via.u64;});
      reader_data.reader_id.assign(integer_view.begin(), integer_view.end());
    }
    if (obj_map.count("issuers") && obj_map["issuers"].type == MSGPACK_OBJECT_ARRAY) {
      msgpack_object_array issuers_array = obj_map["issuers"].via.array;
      reader_data.issuers.resize(issuers_array.size);
      for (uint32_t i = 0; i < issuers_array.size; ++i) {
        unpack_hkIssuer_t(issuers_array.ptr[i], reader_data.issuers[i]);
      }
    }
    if (obj_map.count("revoked_endpoint_keys") && obj_map["revoked_endpoint_keys"].type == MSGPACK_OBJECT_ARRAY) {
        const auto keys = obj_map["revoked_endpoint_keys"].via.array;
        for (uint32_t i = 0; i < keys.size; ++i) {
            if (keys.ptr[i].type != MSGPACK_OBJECT_ARRAY) continue;
            std::vector<uint8_t> key;
            const auto bytes = keys.ptr[i].via.array;
            for (uint32_t j = 0; j < bytes.size; ++j) {
                if (bytes.ptr[j].type != MSGPACK_OBJECT_POSITIVE_INTEGER || bytes.ptr[j].via.u64 > 255) {
                    key.clear(); break;
                }
                key.push_back(bytes.ptr[j].via.u64);
            }
            if (key.size() == 65) reader_data.revoked_endpoint_keys.push_back(std::move(key));
        }
    }
}
