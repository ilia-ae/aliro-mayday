# Snapshot timestamp evidence — 15 October 2026

[Русский](README.ru.md) · [Authorship](../../../AUTHORS.md) · [Verification record](verification.json)

Credit: **Mayday / Ilia Arestov**, for project-original work only. Independent
components retain their authors and licenses.

This package identifies snapshot
[`24e218ef1ea83dafd47447e0393ddd5cd949ccaa`](https://github.com/ilia-ae/aliro-mayday/tree/24e218ef1ea83dafd47447e0393ddd5cd949ccaa),
which contains the updated authorship and license v1.1 website copy. The
[authorship manifest](authorship-manifest.json) binds the commit, its
`SOURCE_MANIFEST.json` (2,396 indexed files), the license and authorship metadata.
Its SHA-256 is:

```text
9f51d5b701905d34c7a22910c80c2950451cfa3cf9402e8485947b90ff13af2c
```

Only the digest and RFC 3161 requests containing that digest and a random nonce
were sent through [deposit.mayday.software](https://deposit.mayday.software/).
Controller keys, backups and private development history are not in this package.

## Verified status

| Evidence | Result at publication |
| --- | --- |
| DigiCert RFC 3161 | Signature, manifest digest and request nonce verified; 2026-10-15 12:14:06 UTC |
| FreeTSA RFC 3161 | Signature, manifest digest and request nonce verified against its published certificates; 2026-10-15 12:14:07 UTC |
| OpenTimestamps Alice, Bob and Finney | Three parseable calendar receipts for the same digest; **Bitcoin attestation pending** |
| Identity signature | No GPG/other author identity signature is claimed |

Verification also rejected modified manifest bytes. An additional Sectigo
response failed full OpenSSL validation and is excluded from the published
verified proofs; the [record](verification.json) notes the failure.

These timestamps establish existence/integrity of this statement and identified
snapshot. They do not independently establish authorship, assign rights, certify
licensing, or establish Aliro certification. See [OpenTimestamps](https://opentimestamps.org/)
and the [copyright background from WIPO](https://www.wipo.int/en/web/copyright/faq-copyright).
The later commit publishing these evidence files is outside the timestamped
snapshot; this avoids a circular claim about the proof timestamping itself.

## Independent verification

Run from this directory. Compare the manifest digest with the value above:

```sh
openssl dgst -sha256 authorship-manifest.json
```

Use a CA bundle you trust for DigiCert, rather than trusting an arbitrary
certificate merely because it is included in a response:

```sh
openssl ts -verify -data authorship-manifest.json \
  -in authorship-manifest.json.tsr.digicert -CAfile /path/to/trusted-ca-bundle.pem
```

FreeTSA's public CA and signing certificate are included. Their file hashes
matched those published on its [official page](https://freetsa.org/index_en.php)
at the time of verification; hashes are retained in `verification.json`.

```sh
openssl ts -verify -data authorship-manifest.json \
  -in authorship-manifest.json.tsr.freetsa \
  -CAfile freetsa-ca.pem -untrusted freetsa-tsa.pem
openssl ts -verify -queryfile authorship-manifest.tsq \
  -in authorship-manifest.json.tsr.freetsa \
  -CAfile freetsa-ca.pem -untrusted freetsa-tsa.pem
```

For OpenTimestamps, inspect a receipt and later upgrade a working copy:

```sh
ots --no-cache info authorship-manifest.json.ots.alice
cp authorship-manifest.json.ots.alice /tmp/aliro-authorship.ots
ots --no-cache upgrade /tmp/aliro-authorship.ots
ots --no-cache verify -f authorship-manifest.json /tmp/aliro-authorship.ots
```

Use a configured Bitcoin verification backend for the final verification. A
pending receipt or successful submission alone is not a verified Bitcoin block.
Check the committed source manifest's SHA-256 against `authorship-manifest.json`
and its individual file hashes against the identified snapshot when validating
the source itself.
