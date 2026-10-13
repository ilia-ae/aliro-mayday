package me.razqqm.smartkey.crypto

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Test
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

class TapSignerTest {

    private val mk = ByteArray(32) { it.toByte() }
    private val nonce = ByteArray(16) { (it + 0x10).toByte() }
    private val endpointId = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8)
    private val timestamp = 0x12345678

    @Test
    fun `tag is 16 bytes`() {
        assertEquals(16, TapSigner.tag(mk, nonce, endpointId, timestamp).size)
    }

    @Test
    fun `tag is deterministic`() {
        val a = TapSigner.tag(mk, nonce, endpointId, timestamp)
        val b = TapSigner.tag(mk, nonce, endpointId, timestamp)
        assertArrayEquals(a, b)
    }

    @Test
    fun `tag matches direct HMAC of spec-fixed transcript`() {
        val expected = run {
            val mac = Mac.getInstance("HmacSHA256")
            mac.init(SecretKeySpec(mk, "HmacSHA256"))
            mac.update("OPEN/v1".toByteArray(Charsets.US_ASCII))
            mac.update(nonce)
            mac.update(endpointId)
            mac.update(byteArrayOf(0x12, 0x34, 0x56, 0x78))
            mac.doFinal().copyOfRange(0, 16)
        }
        assertArrayEquals(expected, TapSigner.tag(mk, nonce, endpointId, timestamp))
    }

    @Test
    fun `tag changes if any input byte changes`() {
        val base = TapSigner.tag(mk, nonce, endpointId, timestamp)

        val otherMk = mk.copyOf().also { it[0] = (it[0] + 1).toByte() }
        assertFalse(base.contentEquals(TapSigner.tag(otherMk, nonce, endpointId, timestamp)))

        val otherNonce = nonce.copyOf().also { it[15] = 0xFF.toByte() }
        assertFalse(base.contentEquals(TapSigner.tag(mk, otherNonce, endpointId, timestamp)))

        val otherEp = endpointId.copyOf().also { it[7] = 0xFF.toByte() }
        assertFalse(base.contentEquals(TapSigner.tag(mk, nonce, otherEp, timestamp)))

        assertFalse(base.contentEquals(TapSigner.tag(mk, nonce, endpointId, timestamp + 1)))
    }

    @Test
    fun `input sizes are enforced`() {
        assertThrows(IllegalArgumentException::class.java) {
            TapSigner.tag(ByteArray(31), nonce, endpointId, timestamp)
        }
        assertThrows(IllegalArgumentException::class.java) {
            TapSigner.tag(mk, ByteArray(15), endpointId, timestamp)
        }
        assertThrows(IllegalArgumentException::class.java) {
            TapSigner.tag(mk, nonce, ByteArray(7), timestamp)
        }
    }

    @Test
    fun `deriveLongTermMac is deterministic and depends on all inputs`() {
        val z = ByteArray(32) { 0x11 }
        val readerId = ByteArray(8) { 0x22 }
        val endpointId2 = ByteArray(8) { 0x33 }

        val a = TapSigner.deriveLongTermMac(z, readerId, endpointId2)
        val b = TapSigner.deriveLongTermMac(z, readerId, endpointId2)
        assertArrayEquals(a, b)
        assertEquals(32, a.size)

        assertFalse(a.contentEquals(
            TapSigner.deriveLongTermMac(z, ByteArray(8) { 0x44 }, endpointId2)))
        assertFalse(a.contentEquals(
            TapSigner.deriveLongTermMac(z, readerId, ByteArray(8) { 0x55 })))
    }

    @Test
    fun `deriveSessionKey returns 32 bytes and enforces nonce size`() {
        assertEquals(32, TapSigner.deriveSessionKey(ByteArray(32), ByteArray(16)).size)
        assertThrows(IllegalArgumentException::class.java) {
            TapSigner.deriveSessionKey(ByteArray(32), ByteArray(15))
        }
    }
}
