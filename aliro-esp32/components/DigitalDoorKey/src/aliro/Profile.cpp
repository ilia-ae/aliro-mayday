#include "aliro/Profile.h"
#include "aliro/AliroKeySchedule.h"
#include "aliro/AliroStdAuth.h"
#include "aliro/AliroStepUp.h"
#include "BerTlv.h"
#include "CommonCryptoUtils.h"
#include "aliro/AliroFastAuth.h"
#include "TLV8.hpp"
#include "ddk/session/AuthOutcome.h"
#include "ddk/store/CredentialStore.h"
#include "DDKLogging.h"
#include "simple_tlv.hpp"
#include <cstring>
#include "aliro/AliroSecureContext.h"
#include "ddk/store/ReaderIdentity.h"
#include "TlvTags.h"

namespace ddk::aliro {

constexpr const char* TAG = "AliroProfile";

Profile::Profile() {}

FailureReason Profile::validate_select(
    Session& session, ddk::span<const uint8_t> select_response)
{
    size_t len = select_response.size();
    if (len >= 2 &&
        select_response[len - 2] == 0x90 && select_response[len - 1] == 0x00)
    {
        len -= 2;
    }

    auto select_parsed = BerTlvMessage::from_bytes(select_response.data(), len);

    const BerTlv* fci = select_parsed.find(0x6F);
    if (!fci) {
        LOG(E, "Aliro SELECT: missing FCI template (0x6F)");
        return FailureReason::VersionMismatch;
    }
    auto fci_inner = fci->parse_inner();
    const BerTlv* proprietary = fci_inner.find(0xA5);
    if (!proprietary) return FailureReason::VersionMismatch;
    auto proprietary_inner = proprietary->parse_inner();
    const BerTlv* version = proprietary_inner.find(0x5C);
    if (!version || version->value.empty() || version->value.size() % 2 != 0) {
        LOG(E, "Aliro SELECT: missing or malformed version list (0x5C)");
        return FailureReason::VersionMismatch;
    }
    bool v10_supported = false;
    bool v9_supported = false;
    for (size_t i = 0; i + 1 < version->value.size(); i += 2) {
        if (version->value[i] == 0x01 && version->value[i + 1] == 0x00) {
            v10_supported = true;
        }
        if (version->value[i] == 0x00 && version->value[i + 1] == 0x09)
            v9_supported = true;
    }
    if (!v10_supported && !(v9_supported && session.config().allow_legacy_apple_v9)) {
        LOG(E, "Aliro v1.0 not in supported versions list (%zu bytes)",
            version->value.size());
        return FailureReason::VersionMismatch;
    }

    const std::array<uint8_t,2> ext_info_tag{0x7F, 0x66};
    const BerTlv* ext_info = proprietary_inner.find(ext_info_tag);
    if (ext_info) {
        auto ext_inner = ext_info->parse_inner();
        const BerTlv* max_recv = ext_inner.find(0x02);
        if (max_recv && !max_recv->value.empty()) {
            max_command_data_size_ = 0;
            for (uint8_t b : max_recv->value)
                max_command_data_size_ = (max_command_data_size_ << 8) | b;
            LOG(I, "Aliro max_command_data_size: %zu", max_command_data_size_);
        }
    }

    const auto& identity = session.store().reader_identity();
    if (identity.group_identifier.size() != 16 ||
        identity.sub_identifier.size() != 16) {
        LOG(E, "Aliro reader identity must be gid(16)+sub(16), got %zu+%zu",
            identity.group_identifier.size(), identity.sub_identifier.size());
        return FailureReason::ChannelError;   // misprovisioned — fail fast
    }

    auto& transcript = session.transcript();

    transcript.protocol_version = v10_supported ? std::array<uint8_t,2>{0x01, 0x00}
                                              : std::array<uint8_t,2>{0x00, 0x09};
    transcript.flags[0] = (session.config().target_flow == Flow::Fast) ? 0x01 : 0x00;
    transcript.flags[1] = session.config().authentication_policy;
    transcript.interface = static_cast<uint8_t>(session.apdu().kind());
    auto& fci_buffer = transcript.fci_proprietary;
    fci_buffer.clear();
    fci_buffer.push_back(0xA5);
    const auto& pv = proprietary->value;
    if (pv.size() < 0x80) {
        fci_buffer.push_back(static_cast<uint8_t>(pv.size()));
    } else if (pv.size() <= 0xFF) {
        fci_buffer.push_back(0x81);
        fci_buffer.push_back(static_cast<uint8_t>(pv.size()));
    } else if (pv.size() <= 0xFFFF) {
        fci_buffer.push_back(0x82);
        fci_buffer.push_back(static_cast<uint8_t>(pv.size() >> 8));
        fci_buffer.push_back(static_cast<uint8_t>(pv.size() & 0xFF));
    }
    fci_buffer.insert(fci_buffer.end(), pv.begin(), pv.end());
    LOG(I, "Aliro version %02X%02X validated, max_cmd=%zu, FCI=%zu bytes",
        transcript.protocol_version[0], transcript.protocol_version[1],
        max_command_data_size_, transcript.fci_proprietary.size());
    return FailureReason::None;
}

FlowState Profile::step(Session& session, FlowState current)
{
  if (current != FlowState::Selected || ran_) return current;
  ran_ = true;

  auto& transcript = session.transcript();
  auto& store = session.store();

  std::array<uint8_t,2> flags = transcript.flags;

  // AUTH0 APDU — Aliro adds 0x41/0x42 flags TLVs before common TLVs
  std::vector<uint8_t> fastTlv;
  auto txflags_tlv = simple_tlv(0x41, std::array<uint8_t,1>{flags[0]});
  std::copy(txflags_tlv.begin(), txflags_tlv.end(), std::back_inserter(fastTlv));
  auto txcode_tlv = simple_tlv(0x42, std::array<uint8_t,1>{flags[1]});
  std::copy(txcode_tlv.begin(), txcode_tlv.end(), std::back_inserter(fastTlv));

  auto version_tlv = simple_tlv(0x5C, transcript.protocol_version);
  std::copy(version_tlv.begin(), version_tlv.end(), std::back_inserter(fastTlv));
  auto reader_pk_tlv = simple_tlv(0x87, transcript.reader_eph_pub);
  std::copy(reader_pk_tlv.begin(), reader_pk_tlv.end(), std::back_inserter(fastTlv));
  auto txId_tlv = simple_tlv(0x4C, transcript.transaction_id);
  std::copy(txId_tlv.begin(), txId_tlv.end(), std::back_inserter(fastTlv));
  auto reader_id_tlv = simple_tlv(0x4D, transcript.reader_identifier);
  std::copy(reader_id_tlv.begin(), reader_id_tlv.end(), std::back_inserter(fastTlv));

  // Aliro: P1=0x00, P2=0x00 (not flags)
  std::vector<uint8_t> apdu{0x80, 0x80, 0x00, 0x00, static_cast<uint8_t>(fastTlv.size())};
  apdu.insert(apdu.end(), std::make_move_iterator(fastTlv.begin()),
              std::make_move_iterator(fastTlv.end()));
  if (transcript.protocol_version != std::array<uint8_t,2>{0x00,0x09} ||
      session.apdu().kind() != TransportKind::Nfc) apdu.push_back(0x00);
                          // Le: case-4 short form — the BLE AP parser rejects
                          // Le-less commands with General Error
  auto response = session.apdu().transceive(apdu);

  if (response.ok() && response.data.size() > 64 && response.data[0] == 0x86) {
    TLV8 Auth0Res;
    Auth0Res.parse(response.data.data(), response.data.size());
    const tlv_t *pubkey = Auth0Res.expect(kEndpoint_Public_Key);
    // SEC1 uncompressed P-256 point: 0x04 prefix || X (32 bytes) || Y (32 bytes).
    constexpr size_t kP256UncompressedPublicKeySize = 1 + 32 + 32;
    if (!Auth0Res.ok() || pubkey == nullptr ||
        pubkey->value.size() != kP256UncompressedPublicKeySize || pubkey->value[0] != 0x04) {
      LOG(E, "Auth0 response is malformed or has an invalid endpoint public key");
      control_flow(session, 0x0, 0x0);
      return FlowState::Failed;
    }
    transcript.endpoint_eph_pub = pubkey->value;
    transcript.endpoint_eph_x = CommonCryptoUtils::get_x(transcript.endpoint_eph_pub);
    ddk::Issuer *foundIssuer = nullptr;
    ddk::Endpoint *foundEndpoint = nullptr;
    ddk::KeyFlow flowUsed = ddk::kFlowFailed;
    bool revocation_failed=false;
    if (session.config().target_flow == Flow::Fast) {
      const tlv_t *crypt = Auth0Res.expect(kAuth0_Cryptogram);
      if (crypt != nullptr) {
        std::vector<uint8_t> encryptedMessage = crypt->value;
        auto fastAuth = AliroFastAuth(session).attest(encryptedMessage);
        if (fastAuth && (flowUsed = fastAuth.flow) == ddk::kFlowFAST) {
            session.set_secure_context(std::make_unique<AliroSecureContext>(
              fastAuth.exchange_sk_reader, fastAuth.exchange_sk_device,
              fastAuth.uwb_ranging_sk, fastAuth.ble_sk));
            if (!on_auth_success(session)) return FlowState::Failed;
            foundIssuer = fastAuth.issuer;
            foundEndpoint = fastAuth.endpoint;
            LOG(D, "Endpoint %s Authenticated via FAST Flow", redactHex("", foundEndpoint->id.data(), foundEndpoint->id.size()).c_str());
        }
      } else {
        LOG(W, "Auth0 cryptogram missing; moving to STANDARD Flow");
      }
    }
    if(foundEndpoint == nullptr){
      AliroStdAuth standard(session);
      auto stdAuth = standard.attest();
        session.set_secure_context(std::make_unique<AliroSecureContext>(
          std::move(stdAuth.gcm_context),
          stdAuth.step_up_sk_reader, stdAuth.step_up_sk_device,
          stdAuth.uwb_ranging_sk, stdAuth.ble_sk));
      if (stdAuth) {
        if (!on_auth_success(session)) return FlowState::Failed;
        foundIssuer = stdAuth.issuer;
        foundEndpoint = stdAuth.endpoint;
        if ((flowUsed = stdAuth.flow) == ddk::kFlowSTANDARD)
        {
          LOG(D, "Endpoint %s Authenticated via STANDARD Flow", redactHex("", foundEndpoint->id.data(), foundEndpoint->id.size()).c_str());
        }
      }
      auto* ctx = static_cast<AliroSecureContext*>(session.secure_context());

      const bool modern_revocation = transcript.protocol_version==std::array<uint8_t,2>{0x01,0x00} &&
          stdAuth.signaling_bitmap && ((*stdAuth.signaling_bitmap)[1]&0x02);
      if (modern_revocation || (foundEndpoint && foundEndpoint->document_bound) ||
          session.config().target_flow == Flow::StepUp ||
          (!stdAuth && stdAuth.endpoint_missing && session.config().step_up_standard_fail)) {
        if (ctx && ctx->step_up_channel()) {
          uint16_t bitmap = static_cast<uint16_t>(
              SignalingBitmask::AccessDocumentRetrievable |
              SignalingBitmask::StepUpSelectRequired);
          if (stdAuth.signaling_bitmap) {
            bitmap = (static_cast<uint16_t>((*stdAuth.signaling_bitmap)[0]) << 8)
                   | (*stdAuth.signaling_bitmap)[1];
          }
          auto scopes = session.config().step_up_scopes.value_or(
              std::map<std::string, bool>{{"matter1", true}});
          auto step = AliroStepUp(session, *ctx, max_command_data_size_).run(
              static_cast<SignalingBitmask>(bitmap), scopes);
          const bool presented_key_matches = !stdAuth.presented_public_key.empty()
              ? stdAuth.presented_public_key == step.endpoint_public_key
              : (stdAuth.key_slot.size() == 8 && [&]() {
                  auto hash = CommonCryptoUtils::hash_identifier_sha1(step.endpoint_public_key);
                  return hash.size() >= 8 && std::equal(stdAuth.key_slot.begin(),
                      stdAuth.key_slot.end(), hash.begin());
                }());
          const auto* required_issuer=foundIssuer?foundIssuer:step.issuer;
          revocation_failed = modern_revocation && (!required_issuer ||
              std::find(step.revocation_issuers.begin(),step.revocation_issuers.end(),required_issuer->public_key)
                  ==step.revocation_issuers.end());
          const bool device_proven = step.success && presented_key_matches &&
              standard.verify_device_signature(step.endpoint_public_key,stdAuth.device_signature);
          if (device_proven && foundEndpoint && foundIssuer==step.issuer) {
              // A signed, device-bound restriction must replace the old cached
              // grant even when this transaction is denied by AccessData.
              if (!foundEndpoint->document_bound || foundEndpoint->access_document!=step.access_document_cbor) {
                  foundEndpoint->document_bound=true;
                  foundEndpoint->access_document=step.access_document_cbor;
              }
          }
          const bool document_allowed = device_proven &&
              (session.config().authorize_step_up_enrollment ?
               session.config().authorize_step_up_enrollment(step.issuer->public_key,
                   step.endpoint_public_key,step.access_document_cbor) : foundEndpoint!=nullptr);
          if (!revocation_failed && document_allowed &&
              (foundEndpoint==nullptr || foundIssuer==step.issuer)) {
            if (step.access_iteration && *step.access_iteration > step.issuer->access_iteration) {
              step.issuer->access_iteration = *step.access_iteration;
              for (auto& endpoint : step.issuer->endpoints)
                if (endpoint.access_iteration < *step.access_iteration &&
                    *step.access_iteration - endpoint.access_iteration >= 8)
                  endpoint.persistent_key.clear();
            }
            flowUsed = ddk::kFlowATTESTATION;
            foundIssuer = step.issuer;
            step_up_access_document_ = step.access_document_cbor;
            if (foundEndpoint == nullptr) {
              ddk::Endpoint endpoint;
              endpoint.public_key = step.endpoint_public_key;
              endpoint.public_key_x.assign(
                  step.endpoint_public_key.begin() + 1,
                  step.endpoint_public_key.begin() + 33);
              auto eId = CommonCryptoUtils::hash_identifier_sha1(
                  step.endpoint_public_key);
              endpoint.id.assign(eId.begin(), eId.begin() + 6);
              foundEndpoint = &*foundIssuer->endpoints.emplace(
                  foundIssuer->endpoints.end(), std::move(endpoint));
            }
            AliroKeySchedule schedule;
            AliroKeySchedule::SessionInput input{
                store.reader_identity().public_key_x,
                transcript.reader_identifier,
                transcript.reader_eph_x,
                transcript.endpoint_eph_x,
                transcript.transaction_id,
                transcript.protocol_version,
                transcript.flags,
                transcript.fci_proprietary,
                transcript.interface,
                transcript.auth0_info_suffix,
            };
            auto Kpersistent = schedule.derive_persistent(
                input, stdAuth.derived_key, foundEndpoint->public_key_x);
            foundEndpoint->persistent_key.assign(Kpersistent.begin(), Kpersistent.end());
            LOG_HEX(D, "StepUp Provisioned Persistent Key",
                    foundEndpoint->persistent_key);
            foundEndpoint->document_bound=true;
            foundEndpoint->access_document=step.access_document_cbor;
            if (step.access_iteration) foundEndpoint->access_iteration = *step.access_iteration;
            foundEndpoint->aliro.documents = step.documents;
          } else if (session.config().target_flow == Flow::StepUp && !stdAuth) {
            flowUsed = ddk::kFlowFailed;
            foundIssuer = nullptr;
            foundEndpoint = nullptr;
          }
        }
      }
      if (foundEndpoint) {
        if (flowUsed == ddk::kFlowSTANDARD) {
          foundEndpoint->persistent_key.assign(stdAuth.persistent_key.begin(), stdAuth.persistent_key.end());
          if (!foundEndpoint->document_bound) foundEndpoint->access_iteration=foundIssuer->access_iteration;
        }
        foundEndpoint->aliro.key_slot = stdAuth.key_slot;
        if (stdAuth.signaling_bitmap) {
          foundEndpoint->aliro.signaling_bitmask =
              (static_cast<uint16_t>((*stdAuth.signaling_bitmap)[0]) << 8)
            | (*stdAuth.signaling_bitmap)[1];
        }
        foundEndpoint->aliro.credential_signed_timestamp =
            stdAuth.credential_signed_timestamp;
        foundEndpoint->aliro.revocation_signed_timestamp =
            stdAuth.revocation_signed_timestamp;
        foundEndpoint->aliro.last_flow = flowUsed;
      }
      if(flowUsed == ddk::kFlowSTANDARD || flowUsed == ddk::kFlowATTESTATION){
        store.save();
      }
    }
    if (foundIssuer && foundEndpoint && !revocation_failed && flowUsed != ddk::kFlowFailed &&
        foundIssuer->document_policy_valid &&
        (!session.config().authorize_authenticated_endpoint ||
         session.config().authorize_authenticated_endpoint(*foundIssuer,*foundEndpoint))) {
      if (!complete(session, ReaderStatus::StateUnsecure)) {
        LOG(W, "Completion delivery failed; auth result stands");
      }
      result_ = {foundIssuer->id, foundEndpoint->id, flowUsed};
      return FlowState::Done;
    }
    if (session.secure_context()) {
      complete(session, ReaderStatus::PublicKeyNotFound);
    } else {
      control_flow(session, 0x00, 0x00);   // no channel — row 9
    }
    return FlowState::Failed;
  }
  control_flow(session, 0x0, 0x0);
  return FlowState::Failed;
}

bool Profile::complete(Session& session, ReaderStatus status)
{
    if (session.transcript().protocol_version == std::array<uint8_t,2>{0x00,0x09} &&
        session.apdu().kind() == TransportKind::Nfc)
        return control_flow(session, 0x01, 0x01).ok();
    auto* ctx = static_cast<AliroSecureContext*>(session.secure_context());
    uint16_t s = static_cast<uint16_t>(status);
    std::vector<uint8_t> payload{0x97, 0x02,
        static_cast<uint8_t>(s >> 8), static_cast<uint8_t>(s & 0xFF)};

    if (ctx) {
        auto resp = ctx->exchange(session, payload, /*skip_response_chaining=*/true);
        if (resp.sw1 == 0x90 || resp.sw1 == 0x61)
            return true;
        LOG(W, "Completion EXCHANGE failed (SW %02X%02X) — CONTROL FLOW fallback",
            resp.sw1, resp.sw2);
    }
    control_flow(session, 0x00, 0x00);
    return false;
}

AuthOutcome Profile::finalize(Session& session)
{
    AuthOutcome outcome;

    if (result_.flow != ddk::kFlowFailed) {
        outcome.state = FlowState::Done;

        // Look up issuer + endpoint by ID in the store
        for (auto& issuer : session.store().issuers()) {
            if (issuer.id == result_.issuer_id) {
                outcome.issuer = &issuer;
                for (auto& endpoint : issuer.endpoints) {
                    if (endpoint.id == result_.endpoint_id) {
                        outcome.endpoint = &endpoint;
                        break;
                    }
                }
                break;
            }
        }
        if (!step_up_access_document_.empty())
            outcome.access_document_cbor = step_up_access_document_;
    } else {
        outcome.state = FlowState::Failed;
        outcome.reason = FailureReason::Auth0Reject;
    }

    return outcome;
}

ApduResponse Profile::control_flow(
    Session& session, uint8_t s1, uint8_t s2)
{
    if (session.transcript().protocol_version == std::array<uint8_t,2>{0x00,0x09} &&
        session.apdu().kind() == TransportKind::Nfc) s2 = 0x01;
    // Aliro CONTROL FLOW: CLA=0x80, INS=0x3C, P1=0x00, P2=0x00
    // Data: BER-TLV body: 0x41 0x01 <s1> 0x42 0x01 <s2>
    std::vector<uint8_t> apdu = {
        0x80, 0x3C, 0x00, 0x00,
        0x06,
        0x41, 0x01, s1,
        0x42, 0x01, s2
    };
    LOG_HEX(D, "Aliro Control Flow APDU", apdu);
    return session.apdu().transceive(apdu);
}

}  // namespace ddk::aliro
