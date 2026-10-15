# Licensing and publication

[Русский](licensing.ru.md) · [License](../LICENSE) · [Third-party notices](../THIRD_PARTY_NOTICES.md)

Reviewed 15 October 2026 against the tracked source. This is a technical inventory
and explanation of the existing terms, not a legal opinion or commercial release
clearance. Original license texts govern. The commercial terms have not changed.

## Project-original code

Original Aliro application code and original website code use the project's
**Source-Available Commercial License v1**. Source is publicly readable; the
repository as a whole is not offered under MIT or an OSI-approved open-source
license. The [OSI definition](https://opensource.org/osd) permits commercial use
without a field-of-use restriction; our original-code terms require a separate
commercial agreement.

| Intended use of project-original code | Existing terms |
| --- | --- |
| Read, study and evaluate the source | Permitted under LICENSE §1(a), (e) |
| Personal, non-production lab use | One copy per household/workstation under §1(b) |
| Academic research, teaching and publication | Permitted with attribution under §1(c) |
| Contributions | §1(d), §6 for original code; the existing license of each contributed file applies |
| Selling devices, paid integration, commercial buildings or services | A separate signed commercial agreement is required under §2 |

The current custom grant does not clearly provide a general right to modify and
redistribute project-original code outside its listed permissions. Obtain written
clarification before advertising broader redistribution rights. Public hosting
does not itself replace a copyright license. Contact: **aliro.mayday@ilia.ae**.

## Component scopes

The project's commercial restrictions apply only to its covered original code.
Separately licensed material retains its permissions, including commercial rights
where its own license grants them. License/copyright notices and more specific
file headers remain applicable.

| Material | Applicable terms |
| --- | --- |
| Root PM3 Python package and MIT-marked publication tools | MIT |
| HomeKey-ESP32 base, inherited firmware/UI, DigitalDoorKey and local changes to those MIT files | MIT, subject to the exceptions below |
| DigitalDoorKey `src/aliro/document_cbor/` | ISC; retained UltraWideLock attribution |
| tinycbor and PN532 components | MIT |
| Espressif components with their Apache license/header | Apache-2.0 |
| Vendored msgpack-c | Boost Software License 1.0 |
| detools | BSD-2-Clause, with its stated MIT exceptions |
| libsodium | ISC base, Apache-2.0 port headers, CC0 Argon2 files and build-macro exception below |

The [notice index](../THIRD_PARTY_NOTICES.md) links each bundled license.
[Inventory](licensing-inventory-2026-10-15.json) records the inspected license texts,
key exceptions and hashes. It covers tracked source, not a complete release SBOM
of downloaded SDKs, packages, fonts or generated artifacts.

## Two exceptions that need explicit notices

**Object diff helper.** Both `aliro-esp32/data/src/lib/utils/objDiff.ts` and
`homekey-esp32/data/src/lib/utils/objDiff.ts` carry CC BY-SA 4.0 notices, attributed
to [Gabriel Gartz's answer](https://stackoverflow.com/a/8432188), with a stated
TypeScript refactor. Stack Exchange's official API reported that license for the
answer during this review. Keep the attribution, source link, license and change
notice, and follow applicable ShareAlike obligations when distributing adaptations.
The commercial license does not add restrictions to this material. Both source
files are unchanged. The full [CC BY-SA 4.0 text](../LICENSES/CC-BY-SA-4.0.txt) is
included; see [the license conditions](https://creativecommons.org/licenses/by-sa/4.0/).

**Autoconf build macros.** Vendored libsodium's `m4/ax_check_link_flag.m4`,
`m4/ax_pthread.m4` and `m4/ax_tls.m4` use GPL-3.0-or-later with the explicit
configure-output exception retained in their headers. That exception permits
copying/modifying/distributing generated configure scripts without applying GPL
to those outputs; other uses of the macro material remain governed by GPL.
The [GPLv3 text](../LICENSES/GPL-3.0.txt) is now included. These build-file terms
must be assessed according to what is actually used and distributed; they do not
establish a repository-wide license. CC0 notices in Argon2 and other headers are
also retained, with the [CC0 text](../LICENSES/CC0-1.0.txt).

## Standards, branding and remaining review

Aliro/Matter badges describe experimental protocol targets and bench scope.
They are plain informational labels, not certified-product marks. The project
does not claim CSA certification, Apple/Samsung approval, or verified support for
all wallets. Refer to the [CSA brand resources and terms](https://csa-iot.org/logo-and-brand-assets-library/).
Our copyright license grants no third-party patent, trademark or certification
rights; applicable standards/IPR programs require separate assessment.

Before distributing commercial devices or generated UI/firmware, review the
complete artifact's dependency/notice obligations, the UI helper's ShareAlike
scope and any standards/IPR requirements with qualified counsel. Resolve the
original-code modification/redistribution grant explicitly. The contribution
clause also needs review for commercial sublicensing of future third-party
contributions: it does not expressly settle those rights or transfer copyright. Adding notices is
not a claim that a full legal/security audit has been completed.

For publication emails use **“source is publicly available”** and link the
repository and this guide. Describe the response delay as time spent clarifying
licensing terms; do not promise unrestricted commercial use or MIT licensing of
the whole repository.
