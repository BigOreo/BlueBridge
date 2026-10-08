/*
    GlideKVM -- mouse and keyboard sharing utility
    Copyright (C) GlideKVM contributors

    This package is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    found in the file LICENSE that should have accompanied this file.

    This package is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

package io.github.bigoreo.glidekvm

import android.content.Context
import android.os.Build
import android.provider.Settings
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import io.github.bigoreo.glidekvm.core.Fingerprint
import io.github.bigoreo.glidekvm.core.TrustedServers
import java.io.File
import java.math.BigInteger
import java.net.Socket
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.Principal
import java.security.PrivateKey
import java.security.cert.X509Certificate
import java.security.spec.ECGenParameterSpec
import java.util.Calendar
import java.util.Date
import javax.net.ssl.KeyManager
import javax.net.ssl.SSLEngine
import javax.net.ssl.X509ExtendedKeyManager
import javax.security.auth.x500.X500Principal

// This device's identity: its name on the desk, and the certificate the main
// computer checks. The key never leaves the Android key store.
object DeviceIdentity {
    private const val ALIAS = "glidekvm-device"
    private const val KEY_STORE = "AndroidKeyStore"

    // A name the main computer accepts: letters, digits, dots, dashes and
    // underscores, as on the desktop's Add a computer.
    fun defaultName(context: Context): String {
        val name = Settings.Global.getString(context.contentResolver, Settings.Global.DEVICE_NAME)
            ?: Build.MODEL
        return sanitizeName(name).ifEmpty { "android" }
    }

    fun sanitizeName(name: String): String =
        name.trim().replace(Regex("\\s+"), "-").replace(Regex("[^A-Za-z0-9._-]"), "").take(64)

    fun trustedServers(context: Context) =
        TrustedServers(File(context.filesDir, "SSL/Fingerprints/TrustedServers.txt"))

    fun fingerprint(): Fingerprint = Fingerprint.of(certificate())

    fun keyManagers(): Array<KeyManager> {
        certificate()
        return arrayOf(StoreKeyManager())
    }

    private fun keyStore(): KeyStore = KeyStore.getInstance(KEY_STORE).apply { load(null) }

    @Synchronized
    private fun certificate(): X509Certificate {
        val store = keyStore()
        (store.getCertificate(ALIAS) as? X509Certificate)?.let { return it }

        val until = Calendar.getInstance().apply { add(Calendar.YEAR, 25) }.time
        val generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, KEY_STORE)
        generator.initialize(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN)
                .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
                .setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256,
                            KeyProperties.DIGEST_SHA384, KeyProperties.DIGEST_SHA512)
                .setCertificateSubject(X500Principal("CN=GlideKVM"))
                .setCertificateSerialNumber(BigInteger.valueOf(System.currentTimeMillis()))
                .setCertificateNotBefore(Date())
                .setCertificateNotAfter(until)
                .build()
        )
        generator.generateKeyPair()
        return keyStore().getCertificate(ALIAS) as X509Certificate
    }

    // Offers the key store's key whenever the main computer asks for a
    // client certificate.
    private class StoreKeyManager : X509ExtendedKeyManager() {
        override fun chooseClientAlias(keyType: Array<String>?, issuers: Array<Principal>?, socket: Socket?) = ALIAS
        override fun chooseEngineClientAlias(keyType: Array<String>?, issuers: Array<Principal>?, engine: SSLEngine?) = ALIAS
        override fun getCertificateChain(alias: String?): Array<X509Certificate> = arrayOf(certificate())
        override fun getPrivateKey(alias: String?): PrivateKey = keyStore().getKey(ALIAS, null) as PrivateKey
        override fun getClientAliases(keyType: String?, issuers: Array<Principal>?) = arrayOf(ALIAS)
        override fun getServerAliases(keyType: String?, issuers: Array<Principal>?): Array<String>? = null
        override fun chooseServerAlias(keyType: String?, issuers: Array<Principal>?, socket: Socket?): String? = null
    }
}
