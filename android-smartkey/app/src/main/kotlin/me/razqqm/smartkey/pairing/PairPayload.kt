package me.razqqm.smartkey.pairing

/**
 * QR-code payload shown by the ESP32 web UI during pairing.
 * See /PROTOCOL.md §2 for the wire format.
 *
 *     ver(1) || reader_id(8) || reader_pub_compressed(33)  =  42 bytes
 *
 * The 42 bytes are base64url'd (no padding) for QR encoding, giving
 * ~56 ASCII chars that fit comfortably in a low error-correction QR.
 */
data class PairPayload(
    val version: Byte,
    val readerId: ByteArray,             // 8 bytes
    val readerPubCompressed: ByteArray,  // 33 bytes (SEC1)
) {
    init {
        require(readerId.size == 8) { "reader_id must be 8 bytes, got ${readerId.size}" }
        require(readerPubCompressed.size == 33) {
            "reader_pub must be 33 bytes, got ${readerPubCompressed.size}"
        }
    }

    fun encode(): ByteArray =
        byteArrayOf(version) + readerId + readerPubCompressed

    override fun equals(other: Any?): Boolean = other is PairPayload &&
        version == other.version &&
        readerId.contentEquals(other.readerId) &&
        readerPubCompressed.contentEquals(other.readerPubCompressed)

    override fun hashCode(): Int {
        var r = version.toInt()
        r = 31 * r + readerId.contentHashCode()
        r = 31 * r + readerPubCompressed.contentHashCode()
        return r
    }

    companion object {
        const val VERSION: Byte = 0x01
        const val WIRE_SIZE: Int = 1 + 8 + 33

        fun decode(raw: ByteArray): PairPayload {
            require(raw.size == WIRE_SIZE) {
                "pair payload must be $WIRE_SIZE bytes, got ${raw.size}"
            }
            val ver = raw[0]
            require(ver == VERSION) {
                "unsupported pair payload version 0x%02X".format(ver)
            }
            return PairPayload(
                version = ver,
                readerId = raw.copyOfRange(1, 9),
                readerPubCompressed = raw.copyOfRange(9, 42),
            )
        }
    }
}
