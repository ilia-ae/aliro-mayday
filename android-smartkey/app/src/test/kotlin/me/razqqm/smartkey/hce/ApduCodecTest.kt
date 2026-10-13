package me.razqqm.smartkey.hce

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ApduCodecTest {

    @Test
    fun `SELECT our AID is parsed as Select`() {
        val apdu = byteArrayOf(
            0x00, 0xA4.toByte(), 0x04, 0x00, 0x08,
            0xF0.toByte(), 0x53, 0x4D, 0x52, 0x54, 0x4B, 0x45, 0x59,
            0x00
        )
        val parsed = ApduCodec.parse(apdu)
        assertTrue("expected Select, got $parsed", parsed is ApduCodec.Incoming.Select)
    }

    @Test
    fun `SELECT other AID (HomeKey) is Unknown`() {
        val apdu = byteArrayOf(
            0x00, 0xA4.toByte(), 0x04, 0x00, 0x07,
            0xA0.toByte(), 0x00, 0x00, 0x08, 0x58, 0x01, 0x01,
            0x00
        )
        assertEquals(ApduCodec.Incoming.Unknown, ApduCodec.parse(apdu))
    }

    @Test
    fun `AUTH challenge parses version and nonce`() {
        val nonce = ByteArray(16) { (it + 0x80).toByte() }
        val apdu = byteArrayOf(
            0x80.toByte(), 0xA0.toByte(), 0x00, 0x00, 0x11,  // Lc = 17 = 0x11
            0x01,  // version
        ) + nonce + byteArrayOf(0x00)  // Le

        val parsed = ApduCodec.parse(apdu)
        assertTrue("expected AuthChallenge, got $parsed",
            parsed is ApduCodec.Incoming.AuthChallenge)
        parsed as ApduCodec.Incoming.AuthChallenge

        assertEquals(0x01.toByte(), parsed.version)
        assertArrayEquals(nonce, parsed.nonce)
    }

    @Test
    fun `AUTH with wrong Lc is Unknown`() {
        val apdu = byteArrayOf(
            0x80.toByte(), 0xA0.toByte(), 0x00, 0x00, 0x20,  // Lc = 32, wrong (should be 17)
        ) + ByteArray(32) + byteArrayOf(0x00)
        assertEquals(ApduCodec.Incoming.Unknown, ApduCodec.parse(apdu))
    }

    @Test
    fun `AUTH with truncated body is Unknown`() {
        val apdu = byteArrayOf(0x80.toByte(), 0xA0.toByte(), 0x00, 0x00, 0x11, 0x01)  // missing nonce
        assertEquals(ApduCodec.Incoming.Unknown, ApduCodec.parse(apdu))
    }

    @Test
    fun `very short APDU is Unknown`() {
        assertEquals(ApduCodec.Incoming.Unknown, ApduCodec.parse(byteArrayOf(0x00)))
        assertEquals(ApduCodec.Incoming.Unknown, ApduCodec.parse(byteArrayOf(0x00, 0xA4.toByte())))
    }

    @Test
    fun `AID constant matches spec bytes`() {
        assertArrayEquals(
            byteArrayOf(0xF0.toByte(), 0x53, 0x4D, 0x52, 0x54, 0x4B, 0x45, 0x59),
            ApduCodec.AID
        )
        assertEquals(8, ApduCodec.AID.size)
    }

    @Test
    fun `encodeAuthResponseBody lays out fields correctly`() {
        val endpointId = byteArrayOf(0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11)
        val tag = ByteArray(16) { 0xCC.toByte() }
        val ts = 0x65800001

        val body = ApduCodec.encodeAuthResponseBody(
            version = Protocol.VERSION,
            endpointId = endpointId,
            timestamp = ts,
            tag = tag,
        )

        assertEquals(ApduCodec.AUTH_RESPONSE_BODY_LEN, body.size)
        assertEquals(29, body.size)
        assertEquals(0x01.toByte(), body[0])
        assertArrayEquals(endpointId, body.copyOfRange(1, 9))
        assertArrayEquals(
            byteArrayOf(0x65, 0x80.toByte(), 0x00, 0x01),
            body.copyOfRange(9, 13),
        )
        assertArrayEquals(tag, body.copyOfRange(13, 29))
    }

    @Test
    fun `encodeAuthResponseBody rejects wrong sizes`() {
        try {
            ApduCodec.encodeAuthResponseBody(
                Protocol.VERSION, ByteArray(7), 0, ByteArray(16))
            error("expected IllegalArgumentException")
        } catch (_: IllegalArgumentException) { /* ok */ }

        try {
            ApduCodec.encodeAuthResponseBody(
                Protocol.VERSION, ByteArray(8), 0, ByteArray(15))
            error("expected IllegalArgumentException")
        } catch (_: IllegalArgumentException) { /* ok */ }
    }
}
