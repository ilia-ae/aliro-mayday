package me.razqqm.smartkey.crypto

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class HkdfTest {

    /** RFC 5869 Appendix A.1 — SHA-256 basic test case. */
    @Test
    fun rfc5869_test_case_1() {
        val ikm = ByteArray(22) { 0x0b }
        val salt = (0..0x0c).map { it.toByte() }.toByteArray()
        val info = (0xf0..0xf9).map { it.toByte() }.toByteArray()
        val expected = hex(
            "3cb25f25faacd57a90434f64d0362f2a" +
            "2d2d0a90cf1a5a4c5db02d56ecc4c5bf" +
            "340072088c6a1df9e0cc"
        )

        assertArrayEquals(expected, Hkdf.derive(ikm, salt, info, length = 42))
    }

    @Test
    fun length_respected() {
        val ikm = ByteArray(16) { 0xAA.toByte() }
        for (n in intArrayOf(1, 16, 32, 64, 128)) {
            assertEquals(n, Hkdf.derive(ikm, ByteArray(0), ByteArray(0), n).size)
        }
    }

    @Test
    fun `zero-length output is rejected`() {
        assertThrows(IllegalArgumentException::class.java) {
            Hkdf.derive(ByteArray(16), ByteArray(0), ByteArray(0), 0)
        }
    }

    private fun hex(s: String): ByteArray =
        s.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
}
