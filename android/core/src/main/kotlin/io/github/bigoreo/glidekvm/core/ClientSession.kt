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

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.nio.charset.StandardCharsets

// What the main computer asks this device to do. Coordinates are in the
// screen size given to the session. Called on the session's thread.
interface ScreenSink {
    // the main computer accepted this device; the mouse can come over now
    fun connected() {}
    fun enter(x: Int, y: Int, modifiers: Int) {}
    fun leave() {}
    fun mouseMove(x: Int, y: Int) {}
    fun mouseRelativeMove(dx: Int, dy: Int) {}
    fun mouseDown(button: Int) {}
    fun mouseUp(button: Int) {}
    // 120 is one notch of a wheel; positive y scrolls up, positive x right
    fun mouseWheel(dx: Int, dy: Int) {}
    fun keyDown(key: Int, modifiers: Int, button: Int) {}
    fun keyRepeat(key: Int, modifiers: Int, count: Int, button: Int) {}
    fun keyUp(key: Int, modifiers: Int, button: Int) {}
    fun clipboardText(text: String) {}
    fun screenSaver(on: Boolean) {}
}

class ScreenShape(val width: Int, val height: Int)

// Why a session ended, in words for the person using the device.
class SessionEndedException(message: String) : IOException(message)

// Runs the client side of one connection: the hello, the screen
// information, then messages until either side hangs up. Blocks in run().
class ClientSession(
    private val stream: PacketStream,
    private val name: String,
    private val shape: () -> ScreenShape,
    private val sink: ScreenSink,
    private val cursor: () -> Pair<Int, Int> = { 0 to 0 },
) {
    @Volatile
    var handshakeComplete = false
        private set

    // the clipboard being received in chunks, per clipboard id
    private val clipboardData = HashMap<Int, ByteArrayOutputStream>()
    private var ignoreMouse = false

    fun run() {
        hello()
        while (true) {
            val packet = try {
                stream.read()
            } catch (e: java.io.EOFException) {
                // hanging up between the hello and the options means the main
                // computer turned this name away; its "unknown" message can be
                // lost as it closes the connection
                if (!handshakeComplete) throw SessionEndedException(notInLayout())
                throw e
            }
            if (!handle(MessageReader(packet))) return
        }
    }

    private fun notInLayout() =
        "The main computer turned \"$name\" away. On it, open Arrange screens, click Add a computer " +
            "and enter $name exactly, then place it next to the main computer and save."


    // Says goodbye; the server takes it as this device leaving.
    fun close() {
        try {
            stream.write(MessageWriter(Protocol.CLOSE).build())
        } catch (_: IOException) {
        }
    }

    // Tells the server the screen changed size, for example on rotation.
    fun screenChanged() {
        ignoreMouse = true
        sendInfo()
    }

    private fun hello() {
        // the main computer hangs up before saying hello when it doesn't
        // trust this device's certificate yet
        val first = try {
            stream.read()
        } catch (e: java.io.EOFException) {
            throw SessionEndedException(NOT_ACCEPTED)
        } catch (e: java.net.SocketTimeoutException) {
            throw SessionEndedException(NOT_ACCEPTED)
        } catch (e: javax.net.ssl.SSLException) {
            throw SessionEndedException(NOT_ACCEPTED)
        }
        val reader = MessageReader(first)
        reader.literal(Protocol.HELLO)
        val major = reader.i16()
        val minor = reader.i16()
        if (major < Protocol.MAJOR_VERSION ||
            (major == Protocol.MAJOR_VERSION && minor < Protocol.MINOR_VERSION)
        ) {
            throw SessionEndedException("The main computer runs an older version ($major.$minor). Update it first.")
        }
        stream.write(
            MessageWriter(Protocol.HELLO)
                .u16(Protocol.MAJOR_VERSION).u16(Protocol.MINOR_VERSION)
                .string(name).build()
        )
    }

    private fun sendInfo() {
        val s = shape()
        val (mx, my) = cursor()
        stream.write(
            MessageWriter(Protocol.INFO)
                .u16(0).u16(0).u16(s.width).u16(s.height)
                .u16(0) // obsolete warp zone size
                .u16(mx).u16(my).build()
        )
    }

    // Returns false when the session is over.
    private fun handle(r: MessageReader): Boolean {
        when (val code = r.code()) {
            Protocol.QUERY_INFO -> sendInfo()
            Protocol.INFO_ACK -> ignoreMouse = false
            Protocol.RESET_OPTIONS -> {}
            Protocol.SET_OPTIONS -> {
                r.intList()
                if (!handshakeComplete) {
                    handshakeComplete = true
                    sink.connected()
                }
            }
            Protocol.KEEP_ALIVE -> stream.write(MessageWriter(Protocol.KEEP_ALIVE).build())
            Protocol.NOOP -> {}
            Protocol.CLOSE -> return false
            Protocol.INCOMPATIBLE -> {
                val major = r.i16()
                val minor = r.i16()
                throw SessionEndedException("The main computer runs an incompatible version ($major.$minor).")
            }
            Protocol.BUSY -> throw SessionEndedException(
                "Another device named \"$name\" is already connected. Give this one a different name."
            )
            Protocol.UNKNOWN -> throw SessionEndedException(notInLayout())
            Protocol.BAD -> throw SessionEndedException("The main computer reported a protocol error.")
            else -> {
                if (!handshakeComplete) throw ProtocolException("unexpected \"$code\" before the handshake")
                handleInput(code, r)
                // answer every input message, as the desktop client does, so a
                // waiting acknowledgement never delays the server
                stream.write(MessageWriter(Protocol.NOOP).build())
            }
        }
        return true
    }

    private fun handleInput(code: String, r: MessageReader) {
        when (code) {
            Protocol.MOUSE_MOVE -> {
                val x = r.i16()
                val y = r.i16()
                if (!ignoreMouse) sink.mouseMove(x, y)
            }
            Protocol.MOUSE_RELATIVE_MOVE -> {
                val dx = r.i16()
                val dy = r.i16()
                if (!ignoreMouse) sink.mouseRelativeMove(dx, dy)
            }
            Protocol.MOUSE_WHEEL -> {
                val dx = r.i16()
                val dy = r.i16()
                sink.mouseWheel(dx, dy)
            }
            Protocol.MOUSE_DOWN -> sink.mouseDown(r.i8())
            Protocol.MOUSE_UP -> sink.mouseUp(r.i8())
            Protocol.KEY_DOWN -> sink.keyDown(r.u16(), r.u16(), r.u16())
            Protocol.KEY_REPEAT -> sink.keyRepeat(r.u16(), r.u16(), r.u16(), r.u16())
            Protocol.KEY_UP -> sink.keyUp(r.u16(), r.u16(), r.u16())
            Protocol.ENTER -> {
                val x = r.i16()
                val y = r.i16()
                r.u32() // sequence number
                val mask = r.u16()
                sink.enter(x, y, mask)
            }
            Protocol.LEAVE -> sink.leave()
            Protocol.GRAB_CLIPBOARD -> {}
            Protocol.SCREEN_SAVER -> sink.screenSaver(r.u8() != 0)
            Protocol.CLIPBOARD_DATA -> clipboardChunk(r)
            // file transfer is not supported on this device yet
            Protocol.FILE_TRANSFER, Protocol.DRAG_INFO -> {}
            else -> throw ProtocolException("unknown message \"$code\"")
        }
    }

    private fun clipboardChunk(r: MessageReader) {
        val id = r.u8()
        r.u32() // sequence number
        val mark = r.u8()
        val data = r.bytes()
        when (mark) {
            Protocol.CLIPBOARD_START -> clipboardData[id] = ByteArrayOutputStream()
            Protocol.CLIPBOARD_CHUNK -> clipboardData[id]?.write(data)
            Protocol.CLIPBOARD_END -> {
                val all = clipboardData.remove(id) ?: return
                // only the main clipboard is shared, not the X11 primary selection
                if (id == 0) textOf(all.toByteArray())?.let { sink.clipboardText(it) }
            }
        }
    }

    companion object {
        const val NOT_ACCEPTED = "The main computer hasn't accepted this device yet. On it, check for a " +
            "question about this device's fingerprint, compare it with the one shown here, and click Yes."

        // The text in a marshalled clipboard: a count of formats, then for each
        // its id, size and data. See IClipboard::marshall.
        fun textOf(clipboard: ByteArray): String? {
            val r = MessageReader(clipboard)
            val formats = r.u32()
            for (i in 0 until formats) {
                val format = r.i32()
                val size = r.u32().toInt()
                if (size > r.remaining) return null
                val start = r.position
                if (format == Protocol.FORMAT_TEXT) {
                    return String(clipboard, start, size, StandardCharsets.UTF_8)
                }
                r.skip(size)
            }
            return null
        }
    }
}
