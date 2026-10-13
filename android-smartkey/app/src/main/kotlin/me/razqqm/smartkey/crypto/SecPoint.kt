package me.razqqm.smartkey.crypto

import org.bouncycastle.asn1.x9.ECNamedCurveTable
import java.math.BigInteger
import java.security.AlgorithmParameters
import java.security.KeyFactory
import java.security.PublicKey
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECPoint
import java.security.spec.ECPublicKeySpec

/**
 * Helpers for encoding/decoding SEC1 compressed P-256 points (33 bytes).
 *
 * Wire format on both sides (ESP32 reader + Android app):
 *   byte 0  = 0x02 (Y is even) or 0x03 (Y is odd)
 *   bytes 1..32 = X coordinate (big-endian, 32 bytes)
 *
 * Used by PROTOCOL.md in pairing QR and per-tap OPEN APDU.
 */
object SecPoint {

    private val jceParams: ECParameterSpec by lazy {
        AlgorithmParameters.getInstance("EC").apply {
            init(ECGenParameterSpec(CURVE))
        }.getParameterSpec(ECParameterSpec::class.java)
    }

    private val bcCurve = ECNamedCurveTable.getByName(CURVE)

    /** Encode a JCE P-256 public key as 33 SEC1-compressed bytes. */
    fun encodeCompressed(pub: ECPublicKey): ByteArray {
        val x = bigIntTo32(pub.w.affineX)
        val prefix = if (pub.w.affineY.testBit(0)) 0x03.toByte() else 0x02.toByte()
        return byteArrayOf(prefix) + x
    }

    /** Decode 33 SEC1-compressed bytes into a JCE P-256 public key. */
    fun decodeCompressed(compressed: ByteArray): PublicKey {
        require(compressed.size == 33) {
            "expected 33 bytes, got ${compressed.size}"
        }
        val prefix = compressed[0].toInt() and 0xFF
        require(prefix == 0x02 || prefix == 0x03) {
            "invalid SEC1 prefix 0x%02X".format(prefix)
        }
        val bcPoint = bcCurve.curve.decodePoint(compressed)
        val jcePoint = ECPoint(
            bcPoint.affineXCoord.toBigInteger(),
            bcPoint.affineYCoord.toBigInteger(),
        )
        return KeyFactory.getInstance("EC")
            .generatePublic(ECPublicKeySpec(jcePoint, jceParams))
    }

    private fun bigIntTo32(v: BigInteger): ByteArray {
        val bytes = v.toByteArray()
        return when {
            bytes.size == 32 -> bytes
            bytes.size == 33 && bytes[0] == 0.toByte() -> bytes.copyOfRange(1, 33)
            bytes.size < 32 -> ByteArray(32 - bytes.size) + bytes
            else -> error("P-256 X coordinate too large: ${bytes.size} bytes")
        }
    }

    private const val CURVE = "secp256r1"
}
