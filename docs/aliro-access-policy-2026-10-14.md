# Aliro document access rights and revocation

[Русский](aliro-access-policy-2026-10-14.ru.md) · [Documentation index](README.md)

14 October 2026. AccessData validation and signed `aliro-r` processing are
integrated into the NFC firmware and tested without the controller. Document
enrollment requires a provisioned issuer, a verified document, the device's
signature and permission to UnlockDoor. Old iPhone records, reader identity and
Matter pairing are preserved.

## Access decision

Cryptographic validation precedes the use of AccessData: ES256, issuer kid,
P-256 device key, AUTH1 signature, the digest of every disclosed element,
namespace, docType, versions, validity and iteration. An unverified certificate
chain creates no trust. Provisioned issuer public keys and UTC tdate with
one-second precision are supported.

AccessData follows Aliro 1.0 §7.3/7.5:

- Version must be 1; the reader action is Unsecure / Matter UnlockDoor.
- At least one inline AccessRule must authorize the action; additional
  ReaderRuleIds are evaluated separately.
- Unknown ReaderRuleIds, empty/invalid rules and unknown critical access
  extensions deny access. Non-critical access extensions and non-access
  extensions confer no permission.
- ID is 1–16 bytes and participates in revocation checks.
- Schedules are an optional standard feature that this firmware does not
  interpret. Each rule referencing AllowScheduleIds or DenyScheduleIds is
  invalid. Another independent rule may authorize the action under the
  standard's OR semantics; the unsupported restriction is never bypassed.
- Firmware does not provision a reader-rule dictionary yet, so ReaderRuleIds
  are rejected. The evaluator can validate explicitly supplied reader rules.

A new endpoint stores the complete verified document and a document-bound flag.
Current permissions, validity and revocation are checked on every Standard/Fast
session and immediately before unlock. A new device-bound signed restriction
replaces the cached grant even when access is denied. Old cache cannot bypass it.

Firmware uses a strict clock policy for document-bound access: access is denied
without a usable system wall-clock, even if the issuer makes time verification
optional. The document cannot establish current time. Old independent
iPhone/Matter keys do not require document time. The clock comes from the system
time service; tests supply it explicitly.

## Signed revocation documents

`aliro-r` undergoes the same signature, digest and validity checks and must not
contain deviceKeyInfo. Applying it does not depend on granting access to the
presenting device: the document may revoke another device.

RevocationData validates version, ChangeMode and entries. SHA-256 of the complete
SEC1 public key, ID, expiration, Overwrite and Update are supported:

- A newer Overwrite iteration applies a snapshot; an equal iteration is ignored.
  Older documents are rejected. An empty newer Overwrite clears the list.
- Update adds entries before removing them. Stored RevocationIteration remains
  unchanged for Update, as required by §7.6.3; each entry also stores its
  iteration. Future Update entries survive an earlier Overwrite.
- An identical entry remains unchanged; a matching hash or ID replaces it.
- Expired entries are skipped with known time. With no clock, an expiring
  revocation remains active. An expiry-bearing removal does not lift revocation.
- Each issuer has at most 64 entries. Invalid data or capacity overflow never
  applies a partial result.

Document-bound keys are checked within their proven issuer's scope. Legacy
direct/cached keys without that binding are conservatively checked against
public-key revocations from all trusted issuers. An empty/first issuer record
cannot hide the same key's revocation. Cache records with the same issuer public
key are aliases of one issuer: revocation and iteration cannot be isolated
between them. Export preserves the credential indices and endpoint metadata of
the specific record instead of borrowing another alias's metadata.

The local prohibition after ClearCredential is stored separately. Signed Update
or Overwrite cannot lift it; explicit reprovisioning through the existing Matter
path can.

## Persistence and compatibility

NVS gains optional document and revocation fields. Old records are read without
migrating identity or pairing. Missing document state does not turn an old
authorized independent key into a document-bound key.

Policy changes persist separately from successful access, with revision checks,
and cannot add an unknown endpoint. An old NFC session cannot resurrect a revoked
key. NVS failure never reports success: denial remains in RAM and affected issuers
are blocked. Recovery requires a successfully persisted, fully verified
revocation snapshot. Successfully stored lists survive reboot; failed storage
does not promise survival across power loss.

Revocation deletes the associated FastAuth secret. After revocation is lifted,
the key must pass Standard first to obtain a new secret. A new access iteration
can likewise require Standard for a previously explicitly provisioned key;
FastAuth then resumes. A document-bound key needs a document for a new iteration.

For Aliro `0x0100`, advertised revocation forces Fast to Standard and requires a
verified document of the key's issuer. Another issuer's document cannot satisfy
that request. Legacy Apple `0x0009` gets no new automatic exchange: its
AUTH0/AUTH1, CONTROL FLOW and key derivation remain. Stored revocation still
applies to v9.

The reader enforces received signed revocations. An entirely offline reader
cannot learn about a document that has not been delivered. Existing
Matter/ClearCredential revocation continues to work.

## Verification

The supported suite passes **15/15 executables** under AddressSanitizer and
UndefinedBehaviorSanitizer with no reported memory errors. The full suite passes
15/16: BLE-flow retains two upstream functional failures (completion status and
duplicate Time Sync). BLE access is disabled in firmware. LeakSanitizer is not
available on the tested macOS runtime. Firmware builds with ESP-IDF 5.4.4 /
esp-matter 8d56152.

The application image is **2,564,864 bytes**, leaving **56,576 bytes (2.16%)** in
the existing OTA partition. SHA-256:
`dc8b7408b67c80520963468e48be29675f56152bb23d735229d5d49c81bf2977`.
The targeted suite has **36 firmware integration cases**, **9 cryptographic
document-validation cases** and **10 AccessData/revocation cases**. It compiles
the actual adapter and NVS serializer and the firmware's vendored tinycbor.
Coverage includes GET RESPONSE chaining, rights/clock/expiry, forged signatures,
replay, persistence on denied access, reboot, NVS failure, local tombstones and
v9/FastAuth preservation.

The controller has a verified private backup. Application-only installation and
rollback preserve data partitions. Updated-firmware iPhone acceptance is separate
from startup checks; Samsung Wallet/SmartThings remains deferred.

Source: [Aliro Specification 1.0, §7.3–7.6](https://csa-iot.org/wp-content/uploads/2026/02/26-42802-001_Aliro_1.0_specification.pdf).
The [initial validator port](aliro-porting-2026-10-13.md) is historical first-stage
context, not the current access-policy status.

The additional Ubuntu/Clang run found a null-pointer memcpy for an empty CBOR
payload. The fix preserves canonical bytes and required-size accounting; a
separate regression checks both boundaries.
[TinyCBOR patch](../aliro-esp32/components/cbor/LOCAL_PATCHES.md).

An additional 22 cases exercise actual provisioning callbacks;
[bench preparation](samsung-bench-preparation-2026-10-14.md) documents the new
reader configuration record, storage failures and acceptance order.
