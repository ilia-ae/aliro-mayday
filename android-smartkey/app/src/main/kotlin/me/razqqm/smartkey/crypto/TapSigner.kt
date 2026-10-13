package me.razqqm.smartkey.crypto

import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * Stateless signer for SmartKey v1 per-tap auth.  See /PROTOCOL.md §3.
 *
 * Two active functions:
 *   - [deriveLongTermMac] — at pairing time, derives `mk` from ECDH.
 *   - [tag] — per tap, produces the 16-byte HMAC tag for the AUTH response.
 *
 * [deriveSessionKey] is a dormant helper for future encrypted-payload
 * use cases (revoke-on-tap, rename-on-tap).  Not on the happy path
 * today because the AUTH flow is HMAC-only — see PROTOCOL.md §3.5.
 *
 * JVM-testable — no AndroidKeyStore dependency.  On-device, `mk` comes
 * from [KeyStoreManager.ecdh] + [deriveLongTermMac] and is persisted
 * alongside the credential record.
 */
object TapSigner {

    /**
     * Tag over the reader's challenge and the phone's response fields.
     *     tag = HMAC-SHA256(mk, "OPEN/v1" || nonce || endpoint_id || ts_be)[0..15]
     *
     * @param mk          32-byte long-term MAC key
     * @param nonce       16 bytes, from the reader's AUTH challenge APDU
     * @param endpointId  8 bytes, this phone's identifier with the reader
     * @param timestamp   phone's current Unix seconds (Int is fine until 2038)
     */
    fun tag(
        mk: ByteArray,
        nonce: ByteArray,
        endpointId: ByteArray,
        timestamp: Int,
    ): ByteArray {
        require(mk.size == 32) { "mk must be 32 bytes, got ${mk.size}" }
        require(nonce.size == 16) { "nonce must be 16 bytes, got ${nonce.size}" }
        require(endpointId.size == 8) { "endpoint_id must be 8 bytes, got ${endpointId.size}" }

        val mac = Mac.getInstance(HMAC_ALG)
        mac.init(SecretKeySpec(mk, HMAC_ALG))
        mac.update(CTX_OPEN)
        mac.update(nonce)
        mac.update(endpointId)
        mac.update(byteArrayOf(
            (timestamp ushr 24).toByte(),
            (timestamp ushr 16).toByte(),
            (timestamp ushr 8).toByte(),
            timestamp.toByte(),
        ))
        return mac.doFinal().copyOfRange(0, 16)
    }

    /**
     * Long-term MAC key, derived once per phone-reader pair.
     *     mk = HKDF-SHA256(Z_pair, salt=reader_id||endpoint_id, info="SmartKey/v1/mac", L=32)
     */
    fun deriveLongTermMac(
        ecdhShared: ByteArray,
        readerId: ByteArray,
        endpointId: ByteArray,
    ): ByteArray {
        require(readerId.size == 8) { "reader_id must be 8 bytes" }
        require(endpointId.size == 8) { "endpoint_id must be 8 bytes" }
        return Hkdf.derive(
            ikm = ecdhShared,
            salt = readerId + endpointId,
            info = INFO_LONG_TERM,
            length = 32,
        )
    }

    /**
     * Future per-tap session key for encrypted payloads.  Currently not
     * used on the happy path — kept so revoke/rename-over-NFC can adopt
     * it without re-introducing the HKDF plumbing.
     */
    fun deriveSessionKey(zTap: ByteArray, nonce: ByteArray): ByteArray {
        require(nonce.size == 16) { "nonce must be 16 bytes, got ${nonce.size}" }
        return Hkdf.derive(
            ikm = zTap,
            salt = nonce,
            info = INFO_SESSION,
            length = 32,
        )
    }

    private const val HMAC_ALG = "HmacSHA256"
    private val CTX_OPEN = "OPEN/v1".toByteArray(Charsets.US_ASCII)
    private val INFO_LONG_TERM = "SmartKey/v1/mac".toByteArray(Charsets.US_ASCII)
    private val INFO_SESSION = "SmartKey/v1/tap".toByteArray(Charsets.US_ASCII)
}
