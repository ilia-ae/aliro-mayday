# DigitalDoorKey revision and local patches

Source: [rednblkx/DigitalDoorKey](https://github.com/rednblkx/DigitalDoorKey).
Pinned revision: [80e4a1e14eab3646b62ab3bdad6d2ed01d399375](https://github.com/rednblkx/DigitalDoorKey/tree/80e4a1e14eab3646b62ab3bdad6d2ed01d399375).
License: MIT, retained in LICENSE. Our changes to this component use MIT.

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

**Access decision boundary:** upstream Step-up verifies issuer/device signatures,
but currently lacks full IssuerSignedItem digest, validity, iteration and access
policy validation. A successful upstream Step-up result is not sufficient to
grant door access. The firmware adapter in `main/DDKAuthContext.cpp` rejects
unknown endpoint keys and commits only already provisioned/cached credentials.

The pinned upstream suite has two known failures in `aliro_ble_flow_test`:
completion-status expectation and duplicate Time Sync callback count. They
reproduce before our patches; BLE access transport is excluded from this firmware.
