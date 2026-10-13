#pragma once
#include "DDKReaderData.h"

namespace aliro::credentials {
inline void mergeIdentity(readerData_t& existing, const readerData_t& identity) {
    if (existing.reader_pk != identity.reader_pk || existing.reader_gid != identity.reader_gid ||
        existing.reader_id != identity.reader_id) {
        for (auto& issuer : existing.issuers)
            for (auto& endpoint : issuer.endpoints) endpoint.endpoint_prst_k.clear();
    }
    existing.reader_sk = identity.reader_sk;
    existing.reader_pk = identity.reader_pk;
    existing.reader_pk_x = identity.reader_pk_x;
    existing.reader_gid = identity.reader_gid;
    existing.reader_id = identity.reader_id;
}
} // namespace aliro::credentials
