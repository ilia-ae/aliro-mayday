/* SPDX-License-Identifier: ISC */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Complete, bounded deterministic Aliro CBOR. Embedded bstr CBOR is checked separately. */
int aliro_document_cbor_valid(const uint8_t *data, size_t length);
#ifdef __cplusplus
}
#endif
