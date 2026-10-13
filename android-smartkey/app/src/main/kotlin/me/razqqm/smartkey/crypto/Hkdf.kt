package me.razqqm.smartkey.crypto

import org.bouncycastle.crypto.digests.SHA256Digest
import org.bouncycastle.crypto.generators.HKDFBytesGenerator
import org.bouncycastle.crypto.params.HKDFParameters

/**
 * HKDF-SHA256 as specified in RFC 5869.
 *
 * Used in two places in SmartKey v1 (see /PROTOCOL.md):
 *   - pairing: long-term MAC key `mk`
 *   - per-tap: session key `sk`
 */
object Hkdf {

    fun derive(ikm: ByteArray, salt: ByteArray, info: ByteArray, length: Int): ByteArray {
        require(length in 1..8160) { "HKDF-SHA256 output length must be 1..8160" }
        val gen = HKDFBytesGenerator(SHA256Digest())
        gen.init(HKDFParameters(ikm, salt, info))
        val out = ByteArray(length)
        gen.generateBytes(out, 0, length)
        return out
    }
}
