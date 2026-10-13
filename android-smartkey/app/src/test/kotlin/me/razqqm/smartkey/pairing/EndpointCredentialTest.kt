package me.razqqm.smartkey.pairing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class EndpointCredentialTest {

    @Test
    fun `aliasFor produces stable lower-hex alias`() {
        val endpointId = byteArrayOf(0xDE.toByte(), 0xAD.toByte(), 0xBE.toByte(), 0xEF.toByte(),
                                      0x01, 0x02, 0x03, 0x04)
        assertEquals("smartkey_deadbeef01020304", EndpointCredential.aliasFor(endpointId))
    }

    @Test
    fun `aliasFor rejects wrong endpoint_id size`() {
        assertThrows(IllegalArgumentException::class.java) {
            EndpointCredential.aliasFor(ByteArray(7))
        }
    }

    @Test
    fun `data class equality by value`() {
        val a = EndpointCredential(
            endpointId = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8),
            readerId = byteArrayOf(9, 10, 11, 12, 13, 14, 15, 16),
            readerLabel = "Front door",
            keyStoreAlias = "smartkey_0102030405060708",
            createdAt = 1_700_000_000_000L,
        )
        val b = a.copy()
        assertEquals(a, b)
        assertEquals(a.hashCode(), b.hashCode())
    }
}
