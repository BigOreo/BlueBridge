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

import java.io.DataInputStream
import java.io.EOFException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.nio.charset.StandardCharsets

// The wire protocol, as in src/lib/glidekvm/protocol_types.h. Every message is
// a packet: a 4 byte big endian length, then a 4 letter code and its fields.
// Integers are big endian; a string is a 4 byte length and its bytes.
object Protocol {
    const val MAJOR_VERSION = 1
    const val MINOR_VERSION = 6
    const val HELLO = "Barrier"
    const val MAX_MESSAGE_LENGTH = 4 * 1024 * 1024
    const val MAX_STRING_LENGTH = 1024 * 1024

    // the server sends a keep-alive every 3 seconds and gives up after 3 missed
    const val KEEP_ALIVE_SECONDS = 3.0
    const val KEEP_ALIVES_UNTIL_DEATH = 3

    const val NOOP = "CNOP"
    const val CLOSE = "CBYE"
    const val ENTER = "CINN"
    const val LEAVE = "COUT"
    const val GRAB_CLIPBOARD = "CCLP"
    const val SCREEN_SAVER = "CSEC"
    const val RESET_OPTIONS = "CROP"
    const val INFO_ACK = "CIAK"
    const val KEEP_ALIVE = "CALV"
    const val KEY_DOWN = "DKDN"
    const val KEY_REPEAT = "DKRP"
    const val KEY_UP = "DKUP"
    const val MOUSE_DOWN = "DMDN"
    const val MOUSE_UP = "DMUP"
    const val MOUSE_MOVE = "DMMV"
    const val MOUSE_RELATIVE_MOVE = "DMRM"
    const val MOUSE_WHEEL = "DMWM"
    const val CLIPBOARD_DATA = "DCLP"
    const val INFO = "DINF"
    const val SET_OPTIONS = "DSOP"
    const val FILE_TRANSFER = "DFTR"
    const val DRAG_INFO = "DDRG"
    const val QUERY_INFO = "QINF"
    const val INCOMPATIBLE = "EICV"
    const val BUSY = "EBSY"
    const val UNKNOWN = "EUNK"
    const val BAD = "EBAD"

    // clipboard chunks: the total size first, then the data, then the end
    const val CLIPBOARD_START = 1
    const val CLIPBOARD_CHUNK = 2
    const val CLIPBOARD_END = 3

    // clipboard formats, as in IClipboard::EFormat
    const val FORMAT_TEXT = 0
}

class ProtocolException(message: String) : IOException(message)

// Reads the fields of one packet in order.
class MessageReader(private val data: ByteArray) {
    var position = 0
        private set

    val remaining: Int get() = data.size - position

    private fun need(n: Int) {
        if (remaining < n) throw ProtocolException("message too short")
    }

    fun code(): String {
        need(4)
        val s = String(data, position, 4, StandardCharsets.ISO_8859_1)
        position += 4
        return s
    }

    fun literal(text: String) {
        val bytes = text.toByteArray(StandardCharsets.ISO_8859_1)
        need(bytes.size)
        for (b in bytes) {
            if (data[position++] != b) throw ProtocolException("expected \"$text\"")
        }
    }

    fun skip(n: Int) {
        need(n)
        position += n
    }

    fun u8(): Int {
        need(1)
        return data[position++].toInt() and 0xff
    }

    fun i8(): Int = u8().toByte().toInt()

    fun u16(): Int {
        need(2)
        val v = ((data[position].toInt() and 0xff) shl 8) or (data[position + 1].toInt() and 0xff)
        position += 2
        return v
    }

    fun i16(): Int = u16().toShort().toInt()

    fun u32(): Long {
        need(4)
        var v = 0L
        repeat(4) { v = (v shl 8) or (data[position++].toLong() and 0xff) }
        return v
    }

    fun i32(): Int = u32().toInt()

    fun bytes(): ByteArray {
        val n = u32()
        if (n > Protocol.MAX_STRING_LENGTH) throw ProtocolException("string too long")
        need(n.toInt())
        val out = data.copyOfRange(position, position + n.toInt())
        position += n.toInt()
        return out
    }

    fun string(): String = String(bytes(), StandardCharsets.UTF_8)

    fun intList(): IntArray {
        val n = u32()
        if (n * 4 > remaining) throw ProtocolException("list too long")
        return IntArray(n.toInt()) { i32() }
    }
}

// Builds one packet.
class MessageWriter(code: String) {
    private val out = java.io.ByteArrayOutputStream()

    init {
        raw(code)
    }

    fun raw(text: String): MessageWriter {
        out.write(text.toByteArray(StandardCharsets.ISO_8859_1))
        return this
    }

    fun u8(v: Int): MessageWriter {
        out.write(v and 0xff)
        return this
    }

    fun u16(v: Int): MessageWriter {
        out.write((v shr 8) and 0xff)
        out.write(v and 0xff)
        return this
    }

    fun u32(v: Long): MessageWriter {
        for (shift in intArrayOf(24, 16, 8, 0)) out.write(((v shr shift) and 0xff).toInt())
        return this
    }

    fun bytes(b: ByteArray): MessageWriter {
        u32(b.size.toLong())
        out.write(b)
        return this
    }

    fun string(s: String): MessageWriter = bytes(s.toByteArray(StandardCharsets.UTF_8))

    fun build(): ByteArray = out.toByteArray()
}

// Splits a byte stream into packets and back.
class PacketStream(input: InputStream, private val output: OutputStream) {
    private val input = DataInputStream(input)
    private val writeLock = Any()

    fun read(): ByteArray {
        val length = try {
            input.readInt()
        } catch (e: EOFException) {
            throw EOFException("The main computer closed the connection.")
        }
        if (length < 0 || length > Protocol.MAX_MESSAGE_LENGTH) {
            throw ProtocolException("message of $length bytes is too long")
        }
        val packet = ByteArray(length)
        input.readFully(packet)
        return packet
    }

    fun write(packet: ByteArray) {
        val header = byteArrayOf(
            (packet.size shr 24).toByte(), (packet.size shr 16).toByte(),
            (packet.size shr 8).toByte(), packet.size.toByte(),
        )
        synchronized(writeLock) {
            output.write(header + packet)
            output.flush()
        }
    }
}
