package me.razqqm.smartkey.crypto

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import java.security.KeyPairGenerator
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec

class SecPointTest {

    @Test
    fun `encode then decode roundtrip preserves point`() {
        val kpg = KeyPairGenerator.getInstance("EC")
        kpg.initialize(ECGenParameterSpec("secp256r1"))
        val orig = kpg.generateKeyPair().public as ECPublicKey

        val encoded = SecPoint.encodeCompressed(orig)
        assertEquals(33, encoded.size)

        val prefix = encoded[0].toInt() and 0xFF
        assert(prefix == 0x02 || prefix == 0x03) { "got SEC1 prefix 0x%02X".format(prefix) }

        val decoded = SecPoint.decodeCompressed(encoded) as ECPublicKey

        assertEquals(orig.w.affineX, decoded.w.affineX)
        assertEquals(orig.w.affineY, decoded.w.affineY)
    }

    @Test
    fun `decode rejects wrong length`() {
        assertThrows(IllegalArgumentException::class.java) {
            SecPoint.decodeCompressed(ByteArray(32))
        }
        assertThrows(IllegalArgumentException::class.java) {
            SecPoint.decodeCompressed(ByteArray(65))
        }
    }

    @Test
    fun `decode rejects invalid SEC1 prefix`() {
        val bad = ByteArray(33).also { it[0] = 0x04 }  // 0x04 = uncompressed form, not allowed here
        assertThrows(IllegalArgumentException::class.java) {
            SecPoint.decodeCompressed(bad)
        }
    }

    @Test
    fun `multiple keypairs roundtrip correctly`() {
        val kpg = KeyPairGenerator.getInstance("EC")
        kpg.initialize(ECGenParameterSpec("secp256r1"))
        repeat(10) {
            val pub = kpg.generateKeyPair().public as ECPublicKey
            val encoded = SecPoint.encodeCompressed(pub)
            val decoded = SecPoint.decodeCompressed(encoded) as ECPublicKey
            assertEquals(pub.w.affineX, decoded.w.affineX)
            assertEquals(pub.w.affineY, decoded.w.affineY)
            assertArrayEquals(encoded, SecPoint.encodeCompressed(decoded))
        }
    }
}
