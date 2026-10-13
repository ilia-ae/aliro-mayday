package me.razqqm.smartkey.hce

/**
 * Wire codec for SmartKey protocol v1.  See /PROTOCOL.md §3 for the spec.
 *
 * Incoming from reader:
 *   SELECT: 00 A4 04 00 08 F0 53 4D 52 54 4B 45 59 00
 *   AUTH:   80 A0 00 00 11 01 <nonce(16)> 00
 *
 * Outgoing to reader:
 *   SELECT response: 90 00
 *   AUTH response:   01 <endpoint_id(8)> <ts(4)> <tag(16)> 90 00
 */
object ApduCodec {

    /** Our registered AID — `F0SMRTKEY`. */
    val AID: ByteArray = byteArrayOf(
        0xF0.toByte(), 0x53, 0x4D, 0x52, 0x54, 0x4B, 0x45, 0x59
    )

    /** AUTH challenge body length: ver(1) + nonce(16). */
    const val AUTH_BODY_LEN: Int = 1 + 16

    /** Phone's AUTH response body length: ver(1) + endpoint_id(8) + ts(4) + tag(16). */
    const val AUTH_RESPONSE_BODY_LEN: Int = 1 + 8 + 4 + 16

    sealed class Incoming {

        /** SELECT of our AID — phone should reply `9000`. */
        data class Select(val aid: ByteArray) : Incoming() {
            override fun equals(other: Any?): Boolean =
                other is Select && aid.contentEquals(other.aid)
            override fun hashCode(): Int = aid.contentHashCode()
        }

        /** AUTH challenge — reader's nonce.  Phone must sign and reply. */
        data class AuthChallenge(
            val version: Byte,
            val nonce: ByteArray,   // 16 bytes
        ) : Incoming() {
            override fun equals(other: Any?): Boolean =
                other is AuthChallenge && version == other.version && nonce.contentEquals(other.nonce)
            override fun hashCode(): Int = 31 * version.toInt() + nonce.contentHashCode()
        }

        data object Unknown : Incoming()
    }

    fun parse(apdu: ByteArray): Incoming {
        if (apdu.size < 4) return Incoming.Unknown
        val cla = apdu[0]
        val ins = apdu[1]
        return when {
            cla == 0x00.toByte() && ins == 0xA4.toByte() -> parseSelect(apdu)
            cla == 0x80.toByte() && ins == 0xA0.toByte() -> parseAuth(apdu)
            else -> Incoming.Unknown
        }
    }

    private fun parseSelect(apdu: ByteArray): Incoming {
        if (apdu.size < 5) return Incoming.Unknown
        val lc = apdu[4].toInt() and 0xFF
        if (apdu.size < 5 + lc) return Incoming.Unknown
        val aid = apdu.copyOfRange(5, 5 + lc)
        return if (aid.contentEquals(AID)) Incoming.Select(aid) else Incoming.Unknown
    }

    private fun parseAuth(apdu: ByteArray): Incoming {
        if (apdu.size < 5) return Incoming.Unknown
        val lc = apdu[4].toInt() and 0xFF
        if (lc != AUTH_BODY_LEN || apdu.size < 5 + lc) return Incoming.Unknown
        val version = apdu[5]
        val nonce = apdu.copyOfRange(6, 6 + 16)
        return Incoming.AuthChallenge(version, nonce)
    }

    /**
     * Build the AUTH response body per /PROTOCOL.md §3.2.
     * Caller appends `90 00` (or `6A 88` on failure) before returning
     * to Android's HCE dispatcher.
     */
    fun encodeAuthResponseBody(
        version: Byte,
        endpointId: ByteArray,
        timestamp: Int,
        tag: ByteArray,
    ): ByteArray {
        require(endpointId.size == 8) { "endpoint_id must be 8 bytes" }
        require(tag.size == 16) { "tag must be 16 bytes" }
        val out = ByteArray(AUTH_RESPONSE_BODY_LEN)
        out[0] = version
        endpointId.copyInto(out, destinationOffset = 1)
        out[9]  = (timestamp ushr 24).toByte()
        out[10] = (timestamp ushr 16).toByte()
        out[11] = (timestamp ushr 8).toByte()
        out[12] = timestamp.toByte()
        tag.copyInto(out, destinationOffset = 13)
        return out
    }
}

/** Protocol-level constants that apply to every message. */
object Protocol {
    /** SmartKey protocol version byte. */
    const val VERSION: Byte = 0x01
}
