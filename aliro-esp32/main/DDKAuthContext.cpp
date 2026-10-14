#include "DDKAuthContext.h"
#include "ReaderCredentialOps.hpp"
#include "DocumentAccessOps.hpp"
#include "ddk/aliro/ProfileFactory.h"
#include "ddk/session/Profile.h"
#include "ddk/store/CredentialStore.h"
#include "ddk/store/Issuer.h"
#include "ddk/store/ReaderIdentity.h"
#include "ddk/transport/NfcChannel.h"

namespace {
class Store final : public ddk::CredentialStore {
public:
    readerData_t original;
    ddk::ReaderIdentity identity;
    std::vector<ddk::Issuer> entries;
    bool wantsSave = false;
    explicit Store(const readerData_t& data) : original(data) {
        identity.private_key = data.reader_sk;
        identity.public_key = data.reader_pk;
        identity.public_key_x = data.reader_pk_x;
        identity.group_identifier = data.reader_gid;
        identity.sub_identifier = data.reader_id;
        for (const auto& issuer : data.issuers) {
            ddk::Issuer entry;
            entry.id = issuer.issuer_id;
            entry.access_iteration = issuer.access_iteration;
            entry.revocations=issuer.revocations; entry.document_policy_valid=issuer.document_policy_valid;
            entry.public_key = issuer.issuer_pk;
            entry.public_key_x = issuer.issuer_pk_x;
            for (const auto& endpoint : issuer.endpoints) {
                if (aliro::credentials::revoked(data, endpoint.endpoint_pk)) continue;
                ddk::Endpoint ep;
                ep.id = endpoint.endpoint_id;
                ep.access_iteration = endpoint.access_iteration;
                ep.document_bound=endpoint.document_bound; ep.access_document=endpoint.access_document;
                ep.public_key = endpoint.endpoint_pk;
                ep.public_key_x = endpoint.endpoint_pk_x;
                ep.persistent_key = endpoint.endpoint_prst_k;
                ep.used_at = endpoint.last_used_at;
                ep.counter = endpoint.counter;
                entry.endpoints.push_back(std::move(ep));
            }
            entries.push_back(std::move(entry));
        }
        for (auto& issuer:entries)
            for (const auto& alias:entries)
                if (alias.public_key==issuer.public_key)
                    issuer.access_iteration=std::max(issuer.access_iteration,alias.access_iteration);
    }
    const ddk::ReaderIdentity& reader_identity() const override { return identity; }
    void provision_identity(const ddk::ReaderIdentity& value) override { identity = value; }
    ddk::span<ddk::Issuer> issuers() override { return {entries.data(), entries.size()}; }
    void save() override { wantsSave = true; } // commit only after successful finalization
    readerData_t exportPolicyData(std::optional<int64_t> now,bool& changed) const {
        auto data=original; changed=false;
        for (auto& old:data.issuers) {
            auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.public_key==old.issuer_pk && e.id==old.issuer_id;});
            if (entry==entries.end()) continue;
            changed|=!(old.revocations==entry->revocations) || old.document_policy_valid!=entry->document_policy_valid ||
                old.access_iteration!=entry->access_iteration;
            old.revocations=entry->revocations; old.document_policy_valid=entry->document_policy_valid;
            old.access_iteration=entry->access_iteration;
            for (auto& ep:old.endpoints) {
                auto current=std::find_if(entry->endpoints.begin(),entry->endpoints.end(),
                    [&](const auto& e){return e.public_key==ep.endpoint_pk;});
                if (current==entry->endpoints.end()) continue;
                bool document_changed=ep.document_bound!=current->document_bound || ep.access_document!=current->access_document;
                changed|=document_changed || ep.access_iteration!=current->access_iteration;
                ep.document_bound=current->document_bound; ep.access_document=current->access_document;
                ep.access_iteration=current->access_iteration;
                if ((document_changed || !aliro::documents::allowed(old,ep,now) ||
                    current->persistent_key.empty()) && !ep.endpoint_prst_k.empty()) {
                    ep.endpoint_prst_k.clear(); changed=true;
                }
            }
        }
        for (auto& issuer:data.issuers)
            for (auto& ep:issuer.endpoints)
                if (!ep.endpoint_prst_k.empty() && !aliro::documents::allowed(data,issuer.issuer_id,ep.endpoint_id,now)) {
                    ep.endpoint_prst_k.clear(); changed=true;
                }
        return data;
    }
    readerData_t exportData() const {
        auto data = original;
        data.issuers.clear();
        for (const auto& entry : entries) {
            hkIssuer_t issuer;
            for (const auto& old : original.issuers)
                if (old.issuer_pk == entry.public_key && old.issuer_id==entry.id) { issuer = old; break; }
            issuer.issuer_id = entry.id;
            issuer.access_iteration = entry.access_iteration;
            issuer.revocations=entry.revocations; issuer.document_policy_valid=entry.document_policy_valid;
            issuer.issuer_pk = entry.public_key;
            issuer.issuer_pk_x = entry.public_key_x;
            issuer.endpoints.clear();
            for (const auto& ep : entry.endpoints) {
                hkEndpoint_t endpoint;
                for (const auto& old : original.issuers) {
                    if (old.issuer_pk!=entry.public_key || old.issuer_id!=entry.id) continue;
                    for (const auto& previous : old.endpoints)
                        if (previous.endpoint_pk==ep.public_key && previous.endpoint_id==ep.id) endpoint=previous;
                }
                endpoint.endpoint_id = ep.id;
                endpoint.access_iteration = ep.access_iteration;
                endpoint.document_bound=ep.document_bound; endpoint.access_document=ep.access_document;
                endpoint.endpoint_pk = ep.public_key;
                endpoint.endpoint_pk_x = ep.public_key_x;
                endpoint.endpoint_prst_k = ep.persistent_key;
                endpoint.last_used_at = ep.used_at;
                endpoint.counter = ep.counter;
                issuer.endpoints.push_back(std::move(endpoint));
            }
            data.issuers.push_back(std::move(issuer));
        }
        return data;
    }
};

class Pn532Channel final : public ddk::NfcChannel {
public:
    explicit Pn532Channel(const DDKAuthenticationContext::Nfc& callback)
        : ddk::NfcChannel([callback](auto& command, auto& response) {
            return command.size() <= 255 && callback(command, response, false);
        }) {}
    size_t max_command_payload() const override { return 248; }
};
}

struct DDKAuthenticationContext::Impl {
    Store store;
    Save save,savePolicy;
    Clock clock;
    ddk::aliro::AccessPolicy access;
    readerData_t& target;
    std::shared_ptr<ddk::ApduChannel> channel;
    std::unique_ptr<ddk::Session> session;
    std::vector<uint8_t> select;
    bool aliro;
    Impl(DigitalKeyType type, const Nfc& callback, readerData_t& data, const Save& commit,
         const Save& policyCommit,const Clock& documentClock,const ddk::aliro::AccessPolicy& accessPolicy)
        : store(data), save(commit), savePolicy(policyCommit?policyCommit:commit),
          clock(documentClock?documentClock:Clock{aliro::documents::wallClock}),access(accessPolicy),
          target(data), channel(std::make_shared<Pn532Channel>(callback)),
          aliro(type == kAliro) {
        ddk::SessionConfig config;
        config.allow_legacy_apple_v9 = true;
        config.step_up_standard_fail = true;
        config.document_clock=clock;
        config.authorize_step_up_enrollment=[this](auto issuerKey,auto endpointKey,auto document) {
            for (const auto& issuer:store.entries) {
                if (!std::equal(issuerKey.begin(),issuerKey.end(),issuer.public_key.begin(),issuer.public_key.end()) ||
                    !issuer.document_policy_valid || aliro::credentials::revoked(store.original,
                        std::vector<uint8_t>(endpointKey.begin(),endpointKey.end()))) continue;
                ddk::aliro::DocumentPolicy policy;
                policy.allow_time_optional_without_clock=false;
                policy.current_time=clock(); policy.access_iteration=issuer.access_iteration;
                auto verified=ddk::aliro::verify_access_document(document,issuer.public_key,policy,{"matter1"});
                return verified && std::equal(endpointKey.begin(),endpointKey.end(),
                    verified->device_public_key.begin(),verified->device_public_key.end()) &&
                    ddk::aliro::evaluate_access_document(*verified,access,issuer.revocations,policy.current_time).allowed;
            }
            return false;
        };
        config.authorize_authenticated_endpoint=[this](const auto& issuer,const auto& ep) {
            if (!ep.document_bound)
                for (const auto& publisher:store.entries)
                    if (!publisher.document_policy_valid || ddk::aliro::credential_revoked(
                            publisher.revocations,ep.public_key,{},clock())) return false;
            if (ep.document_bound)
                for (const auto& publisher:store.entries)
                    if (publisher.public_key==issuer.public_key &&
                        !aliro::documents::allowed(publisher,ep,clock(),access)) return false;
            return !aliro::credentials::revoked(store.original,ep.public_key) &&
                aliro::documents::allowed(issuer,ep,clock(),access);
        };
        session = std::make_unique<ddk::Session>(channel, store, config);
    }
};

DDKAuthenticationContext::DDKAuthenticationContext(DigitalKeyType type, const Nfc& callback,
    readerData_t& data, const Save& save,const Save& policy,const Clock& clock,
    const ddk::aliro::AccessPolicy& access) : impl_(std::make_unique<Impl>(type,callback,data,save,policy,clock,access)) {}
DDKAuthenticationContext::~DDKAuthenticationContext() = default;
void DDKAuthenticationContext::setSelectResponse(const std::vector<uint8_t>& response) {
    impl_->select = response;
}

std::tuple<std::vector<uint8_t>, std::vector<uint8_t>, KeyFlow>
DDKAuthenticationContext::authenticate(KeyFlow requested) {
    const auto failed = std::make_tuple(std::vector<uint8_t>{}, std::vector<uint8_t>{}, kFlowFailed);
    if (!impl_->aliro || requested == kFlowFailed || requested == kFlowNext) return failed;
    // The precomputed session keeps its fresh ephemeral key and transaction ID.
    // The firmware currently requests Fast; explicit Standard/StepUp support
    // is configured without rebuilding that expensive precomputed material.
    impl_->session->set_target_flow(requested == kFlowFAST ? ddk::Flow::Fast :
        requested == kFlowSTANDARD ? ddk::Flow::Standard : ddk::Flow::StepUp);
    auto profile = ddk::aliro::make_profile();
    if (profile->validate_select(*impl_->session, impl_->select) != ddk::FailureReason::None)
        return failed;
    const bool completed=profile->step(*impl_->session, ddk::FlowState::Selected)==ddk::FlowState::Done;
    bool policyChanged=false;
    auto policyData=impl_->store.exportPolicyData(impl_->clock(),policyChanged);
    if (policyChanged) {
        if (!impl_->savePolicy(policyData)) {
            // The production policy commit retains denials in RAM on error.
            // Standalone callers also receive a fail-closed snapshot.
            for (auto& issuer:policyData.issuers) issuer.document_policy_valid=false;
            impl_->target=std::move(policyData);
            return failed;
        }
        impl_->store.original=policyData; impl_->target=std::move(policyData);
    }
    if (!completed) return failed;
    auto outcome = profile->finalize(*impl_->session);
    if (!outcome || !outcome.issuer || !outcome.endpoint ||
        aliro::credentials::revoked(impl_->store.original, outcome.endpoint->public_key)) return failed;
    // An existing key uses its old independent authorization. A learned key
    // must retain a verified document and pass current AccessData/revocation policy.
    bool alreadyAuthorized=false;
    for (const auto& issuer:impl_->store.original.issuers)
        for (const auto& endpoint:issuer.endpoints)
            alreadyAuthorized|=endpoint.endpoint_pk==outcome.endpoint->public_key;
    if ((!alreadyAuthorized && !outcome.endpoint->document_bound) ||
        !aliro::documents::allowed(*outcome.issuer,*outcome.endpoint,impl_->clock(),impl_->access)) return failed;
    const auto issuerId = outcome.issuer->id;
    const auto endpointId = outcome.endpoint->id;
    if (impl_->store.wantsSave) {
        auto data = impl_->store.exportData();
        if (!impl_->save(data)) return failed;
        impl_->target = std::move(data);
    }
    const auto flow = !impl_->store.wantsSave ? kFlowFAST :
        outcome.access_document_cbor.empty() ? kFlowSTANDARD : kFlowATTESTATION;
    return {issuerId, endpointId, flow};
}
