package me.razqqm.smartkey.hce

import android.nfc.cardemulation.HostApduService
import android.os.Bundle
import android.util.Log
import me.razqqm.smartkey.crypto.TapSigner

/**
 * SmartKey HCE service — /PROTOCOL.md §3.
 *
 * Current wiring:
 *   - SELECT our AID        → respond `9000`
 *   - AUTH challenge        → respond `ver || ep_id || ts || tag || 9000`
 *   - Any other APDU        → `6D00`
 *
 * The `mk` byte array in [signWithStoredCredential] is a placeholder.  In
 * the full build, it's loaded via Room-backed [me.razqqm.smartkey.pairing.EndpointCredential]
 * lookup plus an AndroidKeyStore ECDH + HKDF that happens inside a
 * BiometricPrompt CryptoObject — see TODO.md §6.
 */
class SmartKeyHostApduService : HostApduService() {

    override fun processCommandApdu(commandApdu: ByteArray, extras: Bundle?): ByteArray {
        Log.i(TAG, "APDU in  (${commandApdu.size}B): ${commandApdu.toHex()}")

        val response = when (val parsed = ApduCodec.parse(commandApdu)) {
            is ApduCodec.Incoming.Select -> SW_OK
            is ApduCodec.Incoming.AuthChallenge -> handleAuth(parsed)
            ApduCodec.Incoming.Unknown -> SW_INS_NOT_SUPPORTED
        }

        Log.i(TAG, "APDU out (${response.size}B): ${response.toHex()}")
        return response
    }

    override fun onDeactivated(reason: Int) {
        Log.i(TAG, "deactivated, reason=$reason")
    }

    private fun handleAuth(challenge: ApduCodec.Incoming.AuthChallenge): ByteArray {
        if (challenge.version != Protocol.VERSION) {
            Log.w(TAG, "unsupported protocol version 0x%02X".format(challenge.version))
            return SW_REF_DATA_NOT_FOUND
        }

        val credential = loadCredentialStub() ?: return SW_SECURITY_NOT_SATISFIED
        val mk = signWithStoredCredential(credential) ?: return SW_SECURITY_NOT_SATISFIED

        val now = (System.currentTimeMillis() / 1000L).toInt()
        val tag = TapSigner.tag(
            mk = mk,
            nonce = challenge.nonce,
            endpointId = credential.endpointId,
            timestamp = now,
        )

        val body = ApduCodec.encodeAuthResponseBody(
            version = Protocol.VERSION,
            endpointId = credential.endpointId,
            timestamp = now,
            tag = tag,
        )
        return body + SW_OK
    }

    /**
     * STUB: returns a fixed credential for scaffold testing.  Real impl
     * reads from Room DB, selects by the reader we're talking to (matched
     * via the polling-frame TCI we were woken for), and returns the
     * corresponding [me.razqqm.smartkey.pairing.EndpointCredential].
     */
    private fun loadCredentialStub(): StubCredential? {
        val stubEndpointId = ByteArray(8) { (it + 1).toByte() }
        return StubCredential(endpointId = stubEndpointId)
    }

    /**
     * STUB: returns a placeholder 32-byte `mk`.  Real impl calls
     * [me.razqqm.smartkey.crypto.KeyStoreManager.ecdh] (inside a
     * BiometricPrompt CryptoObject) against the cached reader pubkey,
     * then HKDFs through [TapSigner.deriveLongTermMac].  For now this
     * just returns zero bytes so the scaffold APDU flow is testable
     * end-to-end with a mock reader.
     */
    private fun signWithStoredCredential(credential: StubCredential): ByteArray? {
        // TODO: wire KeyStoreManager.ecdh(credential.keyStoreAlias, readerPub)
        //       + TapSigner.deriveLongTermMac, with BiometricPrompt around
        //       the KeyAgreement.init() call.
        return ByteArray(32)
    }

    private data class StubCredential(val endpointId: ByteArray)

    companion object {
        private const val TAG = "SmartKeyHCE"
        private val SW_OK = byteArrayOf(0x90.toByte(), 0x00)
        private val SW_INS_NOT_SUPPORTED = byteArrayOf(0x6D, 0x00)
        private val SW_SECURITY_NOT_SATISFIED = byteArrayOf(0x69.toByte(), 0x82.toByte())
        private val SW_REF_DATA_NOT_FOUND = byteArrayOf(0x6A.toByte(), 0x88.toByte())
    }
}

internal fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }
