package me.razqqm.smartkey.crypto

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PublicKey
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import javax.crypto.KeyAgreement

/**
 * AndroidKeyStore wrapper for SmartKey v1. See /PROTOCOL.md §2.
 *
 * One P-256 keypair per reader — alias format `smartkey_<endpointIdHex>`.
 * Private keys are hardware-backed (StrongBox-preferred, falls back to TEE)
 * and biometric-bound: every ECDH call requires a fresh BiometricPrompt.
 *
 * Pure Android API — NOT unit-testable on the JVM. Instrumented tests only.
 */
object KeyStoreManager {

    /** Generate a new P-256 keypair under [alias]. Returns the public key. */
    fun createEndpointKey(
        alias: String,
        biometricBound: Boolean = true,
        preferStrongBox: Boolean = true,
    ): ECPublicKey {
        val specBuilder = KeyGenParameterSpec.Builder(
            alias,
            KeyProperties.PURPOSE_AGREE_KEY or KeyProperties.PURPOSE_SIGN,
        ).apply {
            setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
            setDigests(KeyProperties.DIGEST_SHA256)
            if (biometricBound) {
                setUserAuthenticationRequired(true)
                setUserAuthenticationParameters(
                    0,  // 0 = per-operation auth (prompt before every use)
                    KeyProperties.AUTH_BIOMETRIC_STRONG or
                        KeyProperties.AUTH_DEVICE_CREDENTIAL,
                )
            }
            if (preferStrongBox) {
                try {
                    setIsStrongBoxBacked(true)
                } catch (_: Throwable) {
                    // StrongBox not available on this device — continues with TEE.
                }
            }
        }

        val kpg = KeyPairGenerator.getInstance(
            KeyProperties.KEY_ALGORITHM_EC,
            KEYSTORE_PROVIDER,
        )
        kpg.initialize(specBuilder.build())
        return kpg.generateKeyPair().public as ECPublicKey
    }

    fun exists(alias: String): Boolean = keystore().containsAlias(alias)

    fun publicKey(alias: String): ECPublicKey =
        keystore().getCertificate(alias).publicKey as ECPublicKey

    /**
     * ECDH between the phone's private key under [alias] and a reader's public
     * [peerPublicKey]. On biometric-bound keys this throws
     * `UserNotAuthenticatedException` if no biometric is cached — callers must
     * catch it and launch BiometricPrompt.
     *
     * Returns the raw X coordinate of the shared point (32 bytes). Feed it
     * through HKDF before using as a key.
     */
    fun ecdh(alias: String, peerPublicKey: PublicKey): ByteArray {
        val priv = keystore().getKey(alias, null)
            ?: error("no key under alias '$alias'")
        val agreement = KeyAgreement.getInstance("ECDH", KEYSTORE_PROVIDER)
        agreement.init(priv)
        agreement.doPhase(peerPublicKey, true)
        return agreement.generateSecret()
    }

    fun delete(alias: String) {
        keystore().deleteEntry(alias)
    }

    private fun keystore(): KeyStore =
        KeyStore.getInstance(KEYSTORE_PROVIDER).apply { load(null) }

    private const val KEYSTORE_PROVIDER = "AndroidKeyStore"
}
