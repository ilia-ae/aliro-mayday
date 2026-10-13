# UltraWideLock CBOR import

Source: https://github.com/ultrawidelock/ultrawidelock/blob/d6e23761e2ffbaee46db7ea4fd3807b0093e0659/modules/ultrawidelock_cred/src/ultrawidelock_stepup_parse.c

Only the allocation-free CBOR cursor and bounded recursive walker are imported,
under ISC. The complete SDK, hardware drivers, transport and trust verifier are
not imported. Local changes reject non-minimal representations, unsorted or duplicate
map keys, mixed integer/text key maps, invalid UTF-8, floating/simple values not used by Aliro, trailing bytes, nesting above
12 and inputs above 4096 bytes. Each embedded MSO, protected header and tagged item
must be checked separately. This is a structural guard, not an authorization decision.
