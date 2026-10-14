#include "ddk/aliro/DocumentAccess.h"
#include "aliro/DocumentCbor.h"
#include <algorithm>
#include <limits>

namespace ddk::aliro {
namespace {
using namespace cbor_tools;
bool keys(Value map,uint64_t largest) {
    if (!cbor_value_is_map(&map)) return false;
    Value it;
    if (cbor_value_enter_container(&map,&it)!=CborNoError) return false;
    while (!cbor_value_at_end(&it)) {
        auto n=number(it);
        if (!n || *n>largest || !advance(it) || !advance(it)) return false;
    }
    return true;
}
bool active(const RevocationEntry& e,std::optional<int64_t> now) {
    return !e.expiry || !now || *now<static_cast<int64_t>(*e.expiry);
}
bool same_identity(const RevocationEntry& a,const RevocationEntry& b) {
    return (!a.public_key_hash.empty() && a.public_key_hash==b.public_key_hash) ||
           (!a.id.empty() && a.id==b.id);
}
bool rule_allows(const AccessRule& rule,const AccessPolicy& policy) {
    if (!rule.capabilities && !rule.allow_schedules && !rule.deny_schedules) return false;
    if (rule.capabilities && (!*rule.capabilities || !(*rule.capabilities&policy.intended_capability))) return false;
    // §7.3.3 requires rejection when the reader does not support schedules.
    return !rule.allow_schedules && !rule.deny_schedules;
}
std::optional<AccessRule> rule(Value map) {
    if (!keys(map,2)) return {};
    AccessRule out;
    for (unsigned label=0;label<3;label++) {
        auto v=field(map,std::to_string(label));
        if (!v) continue;
        auto n=number(*v);
        if (!n || !*n || *n>(label==0 ? UINT16_MAX : UINT8_MAX)) return {};
        if (label==0) out.capabilities=static_cast<uint16_t>(*n);
        if (label==1) out.allow_schedules=static_cast<uint8_t>(*n);
        if (label==2) out.deny_schedules=static_cast<uint8_t>(*n);
    }
    if (!out.capabilities && !out.allow_schedules && !out.deny_schedules) return {};
    return out;
}
bool extension_structure(Value map,bool access) {
    if (!cbor_value_is_map(&map)) return false;
    size_t count=0;
    if (cbor_value_get_map_length(&map,&count)!=CborNoError || count==0 || count>16) return false;
    Value it;
    if (cbor_value_enter_container(&map,&it)!=CborNoError) return false;
    while (!cbor_value_at_end(&it)) {
        auto vendor=number(it);
        if (!vendor || *vendor>0xFFFFFF || !advance(it) || !cbor_value_is_array(&it)) return false;
        if (cbor_value_get_array_length(&it,&count)!=CborNoError || count==0 || count>16) return false;
        Value extension;
        if (cbor_value_enter_container(&it,&extension)!=CborNoError) return false;
        while (!cbor_value_at_end(&extension)) {
            if (!cbor_value_is_array(&extension) ||
                cbor_value_get_array_length(&extension,&count)!=CborNoError || count!=4) return false;
            Value arg;
            if (cbor_value_enter_container(&extension,&arg)!=CborNoError) return false;
            auto flags=number(arg);
            if (!flags || *flags>255 || (access && *flags!=0)) return false;
            // Unknown non-critical access and every non-access extension are ignored.
            for (int n=0;n<2;n++) {
                if (!advance(arg) || !number(arg)) return false;
            }
            if (!advance(arg) || !bytes(arg) || !advance(extension)) return false;
        }
        if (!advance(it)) return false;
    }
    return true;
}
std::optional<std::vector<uint8_t>> access_element(span<const uint8_t> encoded,
                                                  const AccessPolicy& policy) {
    CborParser parser; Value map;
    if (!parse(encoded,parser,map) || !keys(map,6)) return {};
    auto version=field(map,"0");
    if (!version || number(*version)!=1) return {};
    std::vector<uint8_t> id;
    if (auto idv=field(map,"1")) {
        auto b=bytes(*idv);
        if (!b || b->empty() || b->size()>16) return {};
        id=*b;
    }
    if (auto ext=field(map,"5")) { if (!extension_structure(*ext,false)) return {}; }
    if (auto ext=field(map,"6")) { if (!extension_structure(*ext,true)) return {}; }
    if (auto rules=field(map,"2")) {
        if (!cbor_value_is_array(&*rules)) return {};
        size_t count;
        if (cbor_value_get_array_length(&*rules,&count)!=CborNoError || count==0 || count>8) return {};
        Value it; bool allowed=false;
        if (cbor_value_enter_container(&*rules,&it)!=CborNoError) return {};
        while (!cbor_value_at_end(&it)) {
            auto parsed=rule(it);
            if (!parsed) return {};
            allowed|=rule_allows(*parsed,policy);
            if (!advance(it)) return {};
        }
        if (!allowed) return {};
    }
    if (auto reader_rules=field(map,"4")) {
        if (!cbor_value_is_array(&*reader_rules)) return {};
        size_t count;
        if (cbor_value_get_array_length(&*reader_rules,&count)!=CborNoError || count==0 || count>8) return {};
        Value it; bool allowed=false;
        if (cbor_value_enter_container(&*reader_rules,&it)!=CborNoError) return {};
        while (!cbor_value_at_end(&it)) {
            auto id=number(it);
            if (!id || *id>UINT16_MAX) return {};
            auto found=policy.reader_rules.find(static_cast<uint16_t>(*id));
            if (found==policy.reader_rules.end() || found->second.empty() || found->second.size()>8) return {};
            for (const auto& rule:found->second) allowed|=rule_allows(rule,policy);
            if (!advance(it)) return {};
        }
        if (!allowed) return {};
    }
    // Unreferenced schedules are optional data and cannot change this decision.
    return id;
}
bool entries(Value list,std::vector<RevocationEntry>& result,uint64_t iteration,
             std::optional<int64_t> now,bool remove) {
    if (!cbor_value_is_array(&list)) return false;
    size_t count;
    if (cbor_value_get_array_length(&list,&count)!=CborNoError || count>64) return false;
    Value it;
    if (cbor_value_enter_container(&list,&it)!=CborNoError) return false;
    while (!cbor_value_at_end(&it)) {
        if (!keys(it,2)) return false;
        RevocationEntry e; e.iteration=iteration;
        if (auto h=field(it,"0")) {
            auto hash=bytes(*h); if (!hash || hash->size()!=32) return false; e.public_key_hash=*hash;
        }
        if (auto idv=field(it,"1")) {
            auto id=bytes(*idv); if (!id || id->empty() || id->size()>16) return false; e.id=*id;
        }
        if (e.public_key_hash.empty() && e.id.empty()) return false;
        if (auto expiry=field(it,"2")) {
            auto time=number(*expiry); if (!time || *time>UINT32_MAX) return false;
            e.expiry=static_cast<uint32_t>(*time);
        }
        // Invalid expiry-bearing removals never un-revoke an entry (§7.6.4).
        if (active(e,now) && !(remove && e.expiry)) result.push_back(std::move(e));
        if (!advance(it)) return false;
    }
    return true;
}
bool revocation_extensions(Value map) {
    if (!cbor_value_is_map(&map)) return false;
    Value it; if (cbor_value_enter_container(&map,&it)!=CborNoError) return false;
    while (!cbor_value_at_end(&it)) {
        auto vendor=number(it); if (!vendor || *vendor>0xFFFFFF || !advance(it)) return false;
        if (!cbor_value_is_array(&it)) return false;
        size_t count; if (cbor_value_get_array_length(&it,&count)!=CborNoError || count==0 || count>16) return false;
        Value ext; if (cbor_value_enter_container(&it,&ext)!=CborNoError) return false;
        while (!cbor_value_at_end(&ext)) {
            if (!cbor_value_is_array(&ext) || cbor_value_get_array_length(&ext,&count)!=CborNoError || count!=3) return false;
            Value arg; if (cbor_value_enter_container(&ext,&arg)!=CborNoError || !number(arg) ||
                !advance(arg) || !number(arg) || !advance(arg) || !bytes(arg) || !advance(ext)) return false;
        }
        if (!advance(it)) return false;
    }
    return true;
}
} // namespace

bool credential_revoked(const RevocationState& state,span<const uint8_t> key,
                        span<const uint8_t> id,std::optional<int64_t> now) {
    if (state.entries.empty()) return false;
    auto hash=credential_key_hash(key);
    for (const auto& entry:state.entries) {
        if (!active(entry,now)) continue;
        if ((!hash.empty() && hash==entry.public_key_hash) ||
            (!id.empty() && id.size()==entry.id.size() && std::equal(id.begin(),id.end(),entry.id.begin()))) return true;
    }
    return false;
}
AccessDecision evaluate_access_document(const VerifiedDocument& document,const AccessPolicy& policy,
                                       const RevocationState& revoked,std::optional<int64_t> now) {
    AccessDecision result;
    if (document.revocation || policy.intended_capability==0 ||
        (policy.intended_capability&(policy.intended_capability-1))!=0 ||
        credential_revoked(revoked,document.device_public_key,{},now)) return result;
    for (const auto& item:document.elements) {
        auto id=access_element(item.value,policy);
        if (!id || credential_revoked(revoked,document.device_public_key,*id,now)) continue;
        result.allowed=true; result.ids.push_back(*id);
    }
    return result;
}
bool apply_revocation_document(const VerifiedDocument& document,RevocationState& state,
                               std::optional<int64_t> now,bool require_full_snapshot) {
    if (!document.revocation || document.elements.empty()) return false;
    const uint64_t iteration=document.iteration.value_or(0);
    if (iteration<state.iteration) return false;
    if (require_full_snapshot && iteration<=state.iteration) return false;
    auto updated=state;
    for (const auto& item:document.elements) {
        CborParser parser; Value map;
        if (!parse(item.value,parser,map) || !keys(map,4)) return false;
        auto version=field(map,"0"), modev=field(map,"1"), addv=field(map,"2"), removev=field(map,"3");
        if (!version || number(*version)!=1 || !modev || (!addv && !removev)) return false;
        auto mode=number(*modev);
        if (!mode || *mode>1 || (*mode==0 && removev) || (require_full_snapshot && *mode!=0)) return false;
        if (*mode==1) {
            for (const auto& list:{addv,removev}) {
                if (!list) continue;
                size_t count=0;
                if (!cbor_value_is_array(&*list) ||
                    cbor_value_get_array_length(&*list,&count)!=CborNoError || count==0) return false;
            }
        }
        if (auto ext=field(map,"4")) { if (!revocation_extensions(*ext)) return false; }
        std::vector<RevocationEntry> add,remove;
        if (addv && !entries(*addv,add,iteration,now,false)) return false;
        if (removev && !entries(*removev,remove,iteration,now,true)) return false;
        if (*mode==0 && iteration==state.iteration) continue;
        if (*mode==0) {
            size_t original_count=0; cbor_value_get_array_length(&*addv,&original_count);
            std::erase_if(updated.entries,[&](const auto& e){ return original_count==0 || e.iteration<iteration; });
            updated.iteration=iteration;
        }
        for (const auto& entry:add) {
            const bool identical=std::any_of(updated.entries.begin(),updated.entries.end(),[&](const auto& e) {
                return e.public_key_hash==entry.public_key_hash && e.id==entry.id && e.expiry==entry.expiry;
            });
            if (identical) continue; // §7.6.4: do not change an identical stored entry
            std::erase_if(updated.entries,[&](const auto& e){ return same_identity(e,entry); });
            updated.entries.push_back(entry);
        }
        for (const auto& entry:remove)
            std::erase_if(updated.entries,[&](const auto& e){ return same_identity(e,entry); });
        if (updated.entries.size()>64) return false;
        // Update deliberately leaves stored RevocationIteration unchanged (§7.6.3).
    }
    state=std::move(updated); return true;
}
} // namespace ddk::aliro
