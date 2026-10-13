# DigitalDoorKey revision and local patches

Source: [rednblkx/DigitalDoorKey](https://github.com/rednblkx/DigitalDoorKey).
Pinned revision: [80e4a1e14eab3646b62ab3bdad6d2ed01d399375](https://github.com/rednblkx/DigitalDoorKey/tree/80e4a1e14eab3646b62ab3bdad6d2ed01d399375).
License: MIT, retained in LICENSE. Our changes to the MIT sources use MIT.
The imported `src/aliro/document_cbor/` cursor and its local hardening use ISC;
its own LICENSE and UPSTREAM.md apply.

Local patches:

- Optional Apple NFC version `0x0009`, selected only when advertised and
  explicitly enabled by our firmware. Prefer Aliro `0x0100` when available.
  Keep the previous v9 AUTH0/AUTH1 framing and CONTROL FLOW completion.
- Validate endpoint public key and AUTH1 signature before updating persistent
  keys. Step-up binds the signed document key to the AUTH1 device signature.
  A revocation document cannot act as an enrollment grant; reject error status.
- Include guards, a target-flow setter for precomputed sessions, and complete
  byte redaction in default logs.
- Compile only NFC Aliro sources in this ESP32 firmware; the upstream host
  tests continue to cover the BLE/UWB sources.
- Project integration tests compile the actual firmware compatibility adapter
  and NVS serializer with a simulated NFC endpoint and a storage stub. Harness
  additions support valid unknown keys and mismatched document/device keys.
- Whitespace cleanup in imported sources.

**Access document validation:** local validation now checks standard issuer kid,
ES256, P-256 device key, SHA-256 item digests in their namespaces, document/MSO
types and versions, required validity fields, clock policy and stored issuer
iteration. The parser accepts a bounded subset of deterministic CBOR and UTC
dates at second precision. Unvalidated x5chain and unsupported critical COSE
headers are rejected. A document cannot supply its own trusted current time.

**Access decision:** the firmware now supplies document authorization and
per-authentication policy callbacks. AccessData version/action/rules and known
reader rules are checked; unsupported schedules, unknown reader-rule references
and critical access extensions deny the affected element. Unknown non-critical
and non-access extensions do not grant rights. Document-bound keys retain their
signed document and re-check validity, policy and revocation during Fast/Standard
and at the final Matter gate. The firmware requires an available wall clock for
document-based grants while preserving independent legacy iPhone credentials.

**Signed revocation:** aliro-r forbids deviceKeyInfo; verified Overwrite/Update
changes are kept per issuer and applied independently of successful device
access. Revision-checked policy persistence prevents stale authentication from
restoring a revoked key. Storage failure keeps denials in RAM and requires a
verified full snapshot to recover. Optional NVS fields preserve old records.
No certificate-chain trust is inferred; x5chain remains unsupported and rejected.

Apple v9 framing and normal Fast behavior remain unchanged. Modern advertised
revocation uses Standard/Step-up, and stale independently provisioned persistent
keys can re-establish their epoch through authenticated Standard.

See [document access and revocation notes](../../../docs/aliro-access-policy-2026-10-14.md)
in the repository root for the supported optional features and acceptance checks.

See `docs/aliro-porting-2026-10-13.md` in the repository root for scope,
acceptance checks and the separate esp-aliro/UWB migration plan.

The pinned upstream suite has two known failures in `aliro_ble_flow_test`:
completion-status expectation and duplicate Time Sync callback count. They
reproduce before our patches; BLE access transport is excluded from this firmware.
