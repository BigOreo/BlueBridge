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
import java.security.KeyStore
import javax.net.ssl.KeyManagerFactory
import kotlin.system.exitProcess

// Connects like the Android app does and prints one line per message, for
// tests on a computer:
//
//   java -jar glidekvm-probe.jar --name tablet --pkcs12 cert.p12 \
//       --trusted TrustedServers.txt --size 1280x800 127.0.0.1:24800
fun main(args: Array<String>) {
    var name = "android"
    var pkcs12: String? = null
    var password = ""
    var trusted: String? = null
    var width = 1280
    var height = 800
    var address: String? = null
    var i = 0
    while (i < args.size) {
        when (args[i]) {
            "--name" -> name = args[++i]
            "--pkcs12" -> pkcs12 = args[++i]
            "--password" -> password = args[++i]
            "--trusted" -> trusted = args[++i]
            "--size" -> args[++i].split("x").let { width = it[0].toInt(); height = it[1].toInt() }
            else -> address = args[i]
        }
        i++
    }
    if (pkcs12 == null || trusted == null || address == null) {
        System.err.println("usage: --name NAME --pkcs12 FILE [--password PW] --trusted FILE [--size WxH] HOST[:PORT]")
        exitProcess(64)
    }

    val keyStore = KeyStore.getInstance("PKCS12")
    File(pkcs12).inputStream().use { keyStore.load(it, password.toCharArray()) }
    val keys = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm())
    keys.init(keyStore, password.toCharArray())

    val host = address.substringBefore(":")
    val port = address.substringAfter(":", DEFAULT_PORT.toString()).toInt()

    fun say(line: String) {
        println(line)
        System.out.flush()
    }

    val sink = object : ScreenSink {
        override fun connected() = say("connected")
        override fun enter(x: Int, y: Int, modifiers: Int) = say("enter $x $y $modifiers")
        override fun leave() = say("leave")
        override fun mouseMove(x: Int, y: Int) = say("move $x $y")
        override fun mouseRelativeMove(dx: Int, dy: Int) = say("relmove $dx $dy")
        override fun mouseDown(button: Int) = say("down $button")
        override fun mouseUp(button: Int) = say("up $button")
        override fun mouseWheel(dx: Int, dy: Int) = say("wheel $dx $dy")
        override fun keyDown(key: Int, modifiers: Int, button: Int) =
            say("keydown 0x%04x %d %d".format(key, modifiers, button))
        override fun keyRepeat(key: Int, modifiers: Int, count: Int, button: Int) =
            say("keyrepeat 0x%04x %d %d %d".format(key, modifiers, count, button))
        override fun keyUp(key: Int, modifiers: Int, button: Int) =
            say("keyup 0x%04x %d %d".format(key, modifiers, button))
        override fun clipboardText(text: String) = say("clipboard " + text.replace("\n", "\\n"))
        override fun screenSaver(on: Boolean) = say("screensaver $on")
    }

    val connection = Connection(host, port, name, keys.keyManagers, TrustedServers(File(trusted)),
                                { ScreenShape(width, height) }, sink)
    try {
        say("connecting")
        connection.run()
        say("disconnected")
    } catch (e: UntrustedServerException) {
        say("untrusted " + e.fingerprint.dbLine)
        exitProcess(2)
    } catch (e: Exception) {
        say("error " + (e.message ?: e.toString()))
        exitProcess(1)
    }
}
