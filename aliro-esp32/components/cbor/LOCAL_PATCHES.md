# Local TinyCBOR safety patch

14 October 2026. The vendored TinyCBOR remains under its existing MIT license.
`src/cborencoder.c` now skips the memory-copy operation for a zero-length payload.
The string header, canonical encoded bytes, writer callbacks and required-size
accounting are unchanged. Empty COSE/CBOR byte/text strings may use a null payload;
calling `memcpy` with that pointer is undefined even for length zero.

The first Ubuntu/Clang UBSan run caught this in five existing Step-up/document
tests. The macOS run did not diagnose it. A host regression now checks canonical
empty byte/text strings in an exactly sized buffer and out-of-memory accounting
in a header-only buffer. Host tests and firmware compile this same vendored file.
