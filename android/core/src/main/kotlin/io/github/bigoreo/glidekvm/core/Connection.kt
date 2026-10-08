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

package io.github.bigoreo.glidekvm.core

import java.io.File
import java.io.IOException
import java.net.InetSocketAddress
import java.net.SocketTimeoutException
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import javax.net.ssl.KeyManager
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.X509TrustManager

const val DEFAULT_PORT = 24800

// A certificate fingerprint as the desktop app stores it, "v2:sha256:<hex>".
data class Fingerprint(val sha256: ByteArray) {
    val dbLine: String get() = "v2:sha256:" + sha256.joinToString("") { "%02x".format(it) }

    // as the desktop app shows it, so the two can be compared by eye
    val display: String get() = sha256.joinToString(":") { "%02X".format(it) }

    override fun equals(other: Any?) = other is Fingerprint && other.sha256.contentEquals(sha256)
    override fun hashCode() = sha256.contentHashCode()

    companion object {
        fun of(certificate: X509Certificate) =
            Fingerprint(MessageDigest.getInstance("SHA-256").digest(certificate.encoded))

        fun parse(line: String): Fingerprint? {
            val parts = line.trim().split(":")
            if (parts.size != 3 || parts[0] != "v2" || parts[1] != "sha256") return null
            val hex = parts[2]
            if (hex.length != 64) return null
            return Fingerprint(ByteArray(32) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() })
        }
    }
}

// The main computers this device has confirmed, one per line, in the same
// format as the desktop app's TrustedServers.txt.
class TrustedServers(private val file: File) {
    @Synchronized
    fun contains(fingerprint: Fingerprint): Boolean =
        file.exists() && file.readLines().any { Fingerprint.parse(it) == fingerprint }

    @Synchronized
    fun add(fingerprint: Fingerprint) {
        if (contains(fingerprint)) return
        file.parentFile?.mkdirs()
        file.appendText(fingerprint.dbLine + "\n")
    }
}

// Raised when the main computer's certificate isn't trusted yet. The person
// compares the fingerprint with the one shown on that computer, and if it
// matches, it is added to the trusted servers and the connection retried.
class UntrustedServerException(val fingerprint: Fingerprint) :
    CertificateException("the main computer's certificate is not trusted yet")

private class FingerprintTrustManager(private val trusted: TrustedServers) : X509TrustManager {
    override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) {
        val fingerprint = Fingerprint.of(chain[0])
        if (!trusted.contains(fingerprint)) throw UntrustedServerException(fingerprint)
    }

    override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) {
        throw CertificateException("this device only connects to main computers")
    }

    override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
}

// One encrypted connection to a main computer. run() blocks until it ends and
// throws with a reason the person can read; stop() ends it from another thread.
class Connection(
    private val host: String,
    private val port: Int,
    private val name: String,
    // this device's certificate, which the main computer asks for
    private val keyManagers: Array<KeyManager>,
    private val trusted: TrustedServers,
    private val shape: () -> ScreenShape,
    private val sink: ScreenSink,
    private val cursor: () -> Pair<Int, Int> = { 0 to 0 },
) {
    @Volatile private var socket: SSLSocket? = null
    @Volatile private var session: ClientSession? = null
    @Volatile private var stopping = false

    fun run() {
        val context = SSLContext.getInstance("TLS")
        context.init(keyManagers, arrayOf(FingerprintTrustManager(trusted)), null)
        val s = context.socketFactory.createSocket() as SSLSocket
        socket = s
        try {
            s.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
            s.tcpNoDelay = true
            // the server sends a keep-alive every few seconds; silence for
            // longer than that means it has gone
            s.soTimeout = (Protocol.KEEP_ALIVE_SECONDS * Protocol.KEEP_ALIVES_UNTIL_DEATH * 1000).toInt()
            try {
                s.startHandshake()
            } catch (e: IOException) {
                untrusted(e)?.let { throw it }
                throw e
            }
            val session = ClientSession(PacketStream(s.inputStream, s.outputStream), name, shape, sink, cursor)
            this.session = session
            session.run()
        } catch (e: SocketTimeoutException) {
            if (stopping) return
            throw SessionEndedException("The main computer stopped responding.")
        } catch (e: IOException) {
            if (stopping) return
            throw e
        } finally {
            s.close()
        }
    }

    fun stop() {
        stopping = true
        session?.close()
        socket?.close()
    }

    fun screenChanged() {
        session?.screenChanged()
    }

    private fun untrusted(e: Throwable): UntrustedServerException? {
        var cause: Throwable? = e
        while (cause != null) {
            if (cause is UntrustedServerException) return cause
            cause = cause.cause
        }
        return null
    }

    companion object {
        const val CONNECT_TIMEOUT_MS = 10_000
    }
}
