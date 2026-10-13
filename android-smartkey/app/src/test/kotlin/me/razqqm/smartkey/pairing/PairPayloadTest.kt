package me.razqqm.smartkey.pairing

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class PairPayloadTest {

    @Test
    fun `encode produces 42 bytes with version prefix`() {
        val pp = PairPayload(
            version = PairPayload.VERSION,
            readerId = ByteArray(8) { 0xAA.toByte() },
            readerPubCompressed = ByteArray(33) { 0xBB.toByte() }.also { it[0] = 0x02 },
        )
        val raw = pp.encode()
        assertEquals(PairPayload.WIRE_SIZE, raw.size)
        assertEquals(42, raw.size)
        assertEquals(0x01.toByte(), raw[0])
    }

    @Test
    fun `encode then decode roundtrip`() {
        val original = PairPayload(
            version = PairPayload.VERSION,
            readerId = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8),
            readerPubCompressed = ByteArray(33) { i -> (i + 10).toByte() }.also { it[0] = 0x03 },
        )
        val decoded = PairPayload.decode(original.encode())
        assertEquals(original, decoded)
        assertArrayEquals(original.readerId, decoded.readerId)
        assertArrayEquals(original.readerPubCompressed, decoded.readerPubCompressed)
    }

    @Test
    fun `decode rejects wrong length`() {
        assertThrows(IllegalArgumentException::class.java) {
            PairPayload.decode(ByteArray(41))
        }
        assertThrows(IllegalArgumentException::class.java) {
            PairPayload.decode(ByteArray(43))
        }
    }

    @Test
    fun `decode rejects unknown version`() {
        val raw = ByteArray(42).apply {
            this[0] = 0x02  // unknown
            this[9] = 0x02  // valid SEC1 prefix so field checks don't trip first
        }
        assertThrows(IllegalArgumentException::class.java) {
            PairPayload.decode(raw)
        }
    }

    @Test
    fun `constructor rejects wrong sizes`() {
        assertThrows(IllegalArgumentException::class.java) {
            PairPayload(PairPayload.VERSION, ByteArray(7), ByteArray(33))
        }
        assertThrows(IllegalArgumentException::class.java) {
            PairPayload(PairPayload.VERSION, ByteArray(8), ByteArray(32))
        }
    }
}
