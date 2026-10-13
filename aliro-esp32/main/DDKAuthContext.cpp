#include "DDKAuthContext.h"
#include "ReaderCredentialOps.hpp"
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
            entry.public_key = issuer.issuer_pk;
            entry.public_key_x = issuer.issuer_pk_x;
            for (const auto& endpoint : issuer.endpoints) {
                if (aliro::credentials::revoked(data, endpoint.endpoint_pk)) continue;
                ddk::Endpoint ep;
                ep.id = endpoint.endpoint_id;
                ep.access_iteration = endpoint.access_iteration;
                ep.public_key = endpoint.endpoint_pk;
                ep.public_key_x = endpoint.endpoint_pk_x;
                ep.persistent_key = endpoint.endpoint_prst_k;
                ep.used_at = endpoint.last_used_at;
                ep.counter = endpoint.counter;
                entry.endpoints.push_back(std::move(ep));
            }
            entries.push_back(std::move(entry));
        }
    }
    const ddk::ReaderIdentity& reader_identity() const override { return identity; }
    void provision_identity(const ddk::ReaderIdentity& value) override { identity = value; }
    ddk::span<ddk::Issuer> issuers() override { return {entries.data(), entries.size()}; }
    void save() override { wantsSave = true; } // commit only after successful finalization
    readerData_t exportData() const {
        auto data = original;
        data.issuers.clear();
        for (const auto& entry : entries) {
            hkIssuer_t issuer;
            for (const auto& old : original.issuers)
                if (old.issuer_pk == entry.public_key) { issuer = old; break; }
            issuer.issuer_id = entry.id;
            issuer.access_iteration = entry.access_iteration;
            issuer.issuer_pk = entry.public_key;
            issuer.issuer_pk_x = entry.public_key_x;
            issuer.endpoints.clear();
            for (const auto& ep : entry.endpoints) {
                hkEndpoint_t endpoint;
                for (const auto& old : original.issuers)
                    for (const auto& previous : old.endpoints)
                        if (previous.endpoint_pk == ep.public_key) endpoint = previous;
                endpoint.endpoint_id = ep.id;
                endpoint.access_iteration = ep.access_iteration;
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
    Save save;
    readerData_t& target;
    std::shared_ptr<ddk::ApduChannel> channel;
    std::unique_ptr<ddk::Session> session;
    std::vector<uint8_t> select;
    bool aliro;
    Impl(DigitalKeyType type, const Nfc& callback, readerData_t& data, const Save& commit)
        : store(data), save(commit), target(data), channel(std::make_shared<Pn532Channel>(callback)),
          aliro(type == kAliro) {
        ddk::SessionConfig config;
        config.allow_legacy_apple_v9 = true;
        config.step_up_standard_fail = true;
        session = std::make_unique<ddk::Session>(channel, store, config);
    }
};

DDKAuthenticationContext::DDKAuthenticationContext(DigitalKeyType type, const Nfc& callback,
    readerData_t& data, const Save& save) : impl_(std::make_unique<Impl>(type, callback, data, save)) {}
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
    if (profile->step(*impl_->session, ddk::FlowState::Selected) != ddk::FlowState::Done)
        return failed;
    auto outcome = profile->finalize(*impl_->session);
    if (!outcome || !outcome.issuer || !outcome.endpoint ||
        aliro::credentials::revoked(impl_->store.original, outcome.endpoint->public_key)) return failed;
    // Documents now have cryptographic/digest/validity checks, but AccessData
    // schedules, reader rules and critical extensions still need application
    // interpretation. Keep unknown devices denied until that policy exists.
    // Existing Matter-provisioned and legacy cached iPhone keys keep working.
    bool alreadyAuthorized = false;
    for (const auto& issuer : impl_->store.original.issuers)
        for (const auto& endpoint : issuer.endpoints)
            alreadyAuthorized |= endpoint.endpoint_pk == outcome.endpoint->public_key;
    if (!alreadyAuthorized) return failed;
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
