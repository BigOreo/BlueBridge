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

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.SocketTimeoutException

// A main computer that answered on the local network.
data class FoundServer(val name: String, val host: String, val port: Int) {
    val address: String get() = if (port == DEFAULT_PORT) host else "$host:$port"
}

// Finds main computers on the local network: a question is broadcast on the
// sharing port, and each computer that is sharing answers with its name and
// port. The answer comes from the address that reaches it, so there is no
// need to pick among a computer's many addresses.
object Discovery {
    const val VERSION = 1
    private const val ASK = "GlideKVM?"
    private const val ANSWER = "GlideKVM!"

    fun question(): ByteArray = MessageWriter(ASK).u8(VERSION).build()

    fun parseAnswer(data: ByteArray, length: Int, host: String): FoundServer? = try {
        val r = MessageReader(data.copyOf(length))
        r.literal(ANSWER)
        r.u8()
        val port = r.u16() and 0xffff
        val name = r.string()
        if (port == 0 || name.isEmpty()) null else FoundServer(name, host, port)
    } catch (e: ProtocolException) {
        null
    }

    // Where to send the question: everyone on each local network, and the
    // general broadcast address, which some networks drop.
    fun broadcastAddresses(): List<InetAddress> {
        val result = mutableListOf<InetAddress>()
        try {
            for (nif in NetworkInterface.getNetworkInterfaces()?.toList() ?: emptyList()) {
                if (!nif.isUp || nif.isLoopback) continue
                for (a in nif.interfaceAddresses) {
                    a.broadcast?.let { if (it !in result) result += it }
                }
            }
        } catch (e: Exception) {
            // listing the networks isn't allowed everywhere; the general address still works
        }
        val everyone = InetAddress.getByName("255.255.255.255")
        if (everyone !in result) result += everyone
        return result
    }

    fun find(timeoutMs: Int = 1500, port: Int = DEFAULT_PORT,
             targets: List<InetAddress> = broadcastAddresses()): List<FoundServer> {
        val found = linkedMapOf<String, FoundServer>()
        DatagramSocket().use { socket ->
            socket.broadcast = true
            val question = question()
            // ask twice, as a question can be lost on Wi-Fi
            repeat(2) {
                for (target in targets) {
                    try {
                        socket.send(DatagramPacket(question, question.size, target, port))
                    } catch (e: Exception) {
                        // a network that can't broadcast; try the others
                    }
                }
            }
            val buffer = ByteArray(1024)
            val until = System.currentTimeMillis() + timeoutMs
            while (true) {
                val left = until - System.currentTimeMillis()
                if (left <= 0) break
                socket.soTimeout = left.toInt()
                val packet = DatagramPacket(buffer, buffer.size)
                try {
                    socket.receive(packet)
                } catch (e: SocketTimeoutException) {
                    break
                }
                val host = packet.address.hostAddress ?: continue
                parseAnswer(packet.data, packet.length, host)?.let { found.putIfAbsent("$host:${it.port}", it) }
            }
        }
        return found.values.toList()
    }
}
