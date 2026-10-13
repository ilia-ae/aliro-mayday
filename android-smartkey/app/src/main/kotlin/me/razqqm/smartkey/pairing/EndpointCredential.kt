package me.razqqm.smartkey.pairing

/**
 * Local metadata for one paired reader. The actual private key lives in
 * AndroidKeyStore under [keyStoreAlias]; this record only stores what's
 * needed to look it up and render it in the UI.
 */
data class EndpointCredential(
    val endpointId: ByteArray,   // 8 bytes, assigned at pairing time
    val readerId: ByteArray,     // 8 bytes, from PairPayload
    val readerLabel: String,     // user-provided ("Front door")
    val keyStoreAlias: String,   // "smartkey_<endpointIdHex>"
    val createdAt: Long,         // epoch millis
) {
    init {
        require(endpointId.size == 8)
        require(readerId.size == 8)
    }

    override fun equals(other: Any?): Boolean = other is EndpointCredential &&
        endpointId.contentEquals(other.endpointId) &&
        readerId.contentEquals(other.readerId) &&
        readerLabel == other.readerLabel &&
        keyStoreAlias == other.keyStoreAlias &&
        createdAt == other.createdAt

    override fun hashCode(): Int {
        var r = endpointId.contentHashCode()
        r = 31 * r + readerId.contentHashCode()
        r = 31 * r + readerLabel.hashCode()
        r = 31 * r + keyStoreAlias.hashCode()
        r = 31 * r + createdAt.hashCode()
        return r
    }

    companion object {
        fun aliasFor(endpointId: ByteArray): String {
            require(endpointId.size == 8)
            return "smartkey_" + endpointId.joinToString("") { "%02x".format(it) }
        }
    }
}
