/* SPDX-License-Identifier: ISC
 * Copyright (c) 2026 asxeem and UltraWideLock contributors
 * Copyright (c) 2026 Ilia Arestov (local hardening)
 * Derived from UltraWideLock ultrawidelock_stepup_parse.c at
 * d6e23761e2ffbaee46db7ea4fd3807b0093e0659. See LICENSE and UPSTREAM.md.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "document_cbor.h"

#define CB_MAX_DEPTH 12

/**
 * CBOR stream parser: p (current position), end (buffer limit).
 */
struct cbor {
	const uint8_t *p;
	const uint8_t *end;
};

static int valid_utf8(const uint8_t *p, size_t length)
{
    size_t i = 0;
    while (i < length) {
        uint8_t first = p[i++];
        if (first < 0x80) continue;
        unsigned count;
        uint32_t value, minimum;
        if (first >= 0xc2 && first <= 0xdf) { count=1; value=first&31; minimum=0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count=2; value=first&15; minimum=0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count=3; value=first&7; minimum=0x10000; }
        else return 0;
        if (count > length-i) return 0;
        while (count--) {
            uint8_t tail=p[i++];
            if ((tail&0xc0) != 0x80) return 0;
            value=(value<<6)|(tail&63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    }
    return 1;
}

/* Read one CBOR head: major type + argument. Consumes the argument bytes (and,
 * for major type 7, the simple/float payload). Rejects indefinite lengths and
 * anything truncated. Returns 0 on success. */
static int cb_head(struct cbor *c, uint8_t *mt, uint64_t *arg)
{
	if (c->p >= c->end) {
		return -1;
	}
	uint8_t ib = *c->p++;
	uint8_t ai = ib & 0x1fu;

	*mt = (uint8_t)(ib >> 5);
	if (ai < 24u) {
		*arg = ai;
		return 0;
	}
	size_t need = (ai == 24u)   ? 1u
		      : (ai == 25u) ? 2u
		      : (ai == 26u) ? 4u
		      : (ai == 27u) ? 8u
				    : 0u;

	if (need == 0u || (size_t)(c->end - c->p) < need) {
		return -1; /* 28..31 (incl. indefinite) or truncated */
	}
	uint64_t v = 0;

	for (size_t i = 0; i < need; i++) {
		v = (v << 8) | c->p[i];
	}
	c->p += need;
	/* Local hardening: shortest encoding, no floating/simple extensions. */
	if (*mt == 7 || (need == 1 && v < 24) || (need == 2 && v <= 255) ||
	    (need == 4 && v <= 65535) || (need == 8 && v <= UINT32_MAX)) return -1;
	*arg = v;
	return 0;
}

/**
 * Recursively skip one CBOR value: ints/floats/strings/arrays/maps/tags. Depth-checks CB_MAX_DEPTH.
 * Returns 0 on success, -1 on overflow or malformed.
 */
static int cb_skip_d(struct cbor *c, int depth)
{
	uint8_t mt;
	uint64_t arg;

	if (depth > CB_MAX_DEPTH || cb_head(c, &mt, &arg) != 0) {
		return -1;
	}
	switch (mt) {
	case 0: /* uint */
	case 1: /* nint */
		return 0;
	case 7: /* Only bool/null occur in Aliro documents. */
		return (arg >= 20 && arg <= 22) ? 0 : -1;
	case 2: /* bstr */
	case 3: /* tstr */
		if (arg > (uint64_t)(c->end - c->p)) {
			return -1;
		}
		if (mt == 3 && !valid_utf8(c->p, (size_t)arg)) return -1;
		c->p += (size_t)arg;
		return 0;
	case 4: /* array */
		for (uint64_t i = 0; i < arg; i++) {
			if (cb_skip_d(c, depth + 1) != 0) {
				return -1;
			}
		}
		return 0;
	case 5: { /* RFC 8949 core deterministic: bytewise ordered, unique keys. */
		const uint8_t *previous = NULL;
		size_t previous_len = 0;
		int key_kind = -1;
		for (uint64_t i = 0; i < arg; i++) {
			const uint8_t *key = c->p;
			if (key == c->end) return -1;
			int kind = key[0] >> 5;
			if (kind == 1) kind = 0; /* positive/negative COSE integer labels */
			if ((kind != 0 && kind != 3) || (key_kind >= 0 && key_kind != kind)) return -1;
			key_kind = kind; /* prevent legacy integer/text aliases in one map */
			if (cb_skip_d(c, depth + 1) != 0) return -1;
			size_t len = (size_t)(c->p - key);
			if (previous) {
				size_t common = len < previous_len ? len : previous_len;
				int order = memcmp(previous, key, common);
				if (order > 0 || (order == 0 && previous_len >= len)) return -1;
			}
			previous = key; previous_len = len;
			if (cb_skip_d(c, depth + 1) != 0) return -1;
		}
		return 0;
	}
	case 6: /* tag */
		return cb_skip_d(c, depth + 1);
	default:
		return -1;
	}
}


int aliro_document_cbor_valid(const uint8_t *data, size_t length)
{
    if (!data || length == 0 || length > 4096) return 0;
    struct cbor cursor = {data, data + length};
    return cb_skip_d(&cursor, 0) == 0 && cursor.p == cursor.end;
}
