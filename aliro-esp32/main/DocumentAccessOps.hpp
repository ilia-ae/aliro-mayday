#pragma once
#include "DDKReaderData.h"
#include <ctime>

namespace aliro::documents {
inline std::optional<int64_t> wallClock() {
    const auto now=std::time(nullptr);
    // Boot/uptime is not a valid document clock. The application sets wall time
    // through its time service; document timestamps never set this clock.
    if (now<=1577836800) return {};
    return static_cast<int64_t>(now);
}
template<class Issuer,class Endpoint>
bool allowed(const Issuer& issuer,const Endpoint& endpoint,std::optional<int64_t> now,
             const ddk::aliro::AccessPolicy& access={}) {
    if (!issuer.document_policy_valid) return false;
    if (ddk::aliro::credential_revoked(issuer.revocations,endpoint.public_key,{},now)) return false;
    if (!endpoint.document_bound) return true; // existing explicit/cached iPhone key
    ddk::aliro::DocumentPolicy policy;
    policy.allow_time_optional_without_clock=false;
    policy.current_time=now; policy.access_iteration=issuer.access_iteration;
    auto doc=ddk::aliro::verify_access_document(endpoint.access_document,issuer.public_key,policy,{"matter1"});
    return doc && doc->device_public_key==endpoint.public_key &&
        ddk::aliro::evaluate_access_document(*doc,access,issuer.revocations,now).allowed;
}
inline bool allowed(const hkIssuer_t& issuer,const hkEndpoint_t& endpoint,std::optional<int64_t> now) {
    if (!issuer.document_policy_valid ||
        ddk::aliro::credential_revoked(issuer.revocations,endpoint.endpoint_pk,{},now)) return false;
    if (!endpoint.document_bound) return true;
    ddk::aliro::DocumentPolicy policy;
    policy.allow_time_optional_without_clock=false;
    policy.current_time=now; policy.access_iteration=issuer.access_iteration;
    auto doc=ddk::aliro::verify_access_document(endpoint.access_document,issuer.issuer_pk,policy,{"matter1"});
    return doc && doc->device_public_key==endpoint.endpoint_pk &&
        ddk::aliro::evaluate_access_document(*doc,{},issuer.revocations,now).allowed;
}
inline bool allowed(const readerData_t& data,const std::vector<uint8_t>& issuerId,
                    const std::vector<uint8_t>& endpointId,std::optional<int64_t> now) {
    for (const auto& issuer:data.issuers) {
        if (issuer.issuer_id!=issuerId) continue;
        for (const auto& endpoint:issuer.endpoints) {
            if (endpoint.endpoint_id!=endpointId) continue;
            if (!endpoint.document_bound) {
                // Legacy/direct Matter entries have no cryptographically proved
                // parent issuer. Do not let a placeholder/first-issuer mapping
                // bypass a trusted issuer's explicit revocation of that key.
                for (const auto& publisher:data.issuers)
                    if (!publisher.document_policy_valid || ddk::aliro::credential_revoked(
                            publisher.revocations,endpoint.endpoint_pk,{},now)) return false;
            } else {
                // Two old cache IDs with one public key are the same issuer.
                // Revocation/iteration state on either alias must be enforced.
                for (const auto& publisher:data.issuers)
                    if (publisher.issuer_pk==issuer.issuer_pk &&
                        !allowed(publisher,endpoint,now)) return false;
            }
            return allowed(issuer,endpoint,now);
        }
    }
    return false;
}
} // namespace aliro::documents
