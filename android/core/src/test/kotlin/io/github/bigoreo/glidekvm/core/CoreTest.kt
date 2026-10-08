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
import java.io.File
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.nio.file.Files
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ProtocolTest {
    @Test
    fun writesFieldsBigEndian() {
        val bytes = MessageWriter("DMMV").u16(0x0102).u16(-2).build()
        assertContentEquals(byteArrayOf('D'.code.toByte(), 'M'.code.toByte(), 'M'.code.toByte(),
                                        'V'.code.toByte(), 1, 2, 0xff.toByte(), 0xfe.toByte()), bytes)
    }

    @Test
    fun readsWhatItWrites() {
        val bytes = MessageWriter("TEST").u8(7).u16(-300).u32(0x01020304).string("héllo").build()
        val r = MessageReader(bytes)
        assertEquals("TEST", r.code())
        assertEquals(7, r.u8())
        assertEquals(-300, r.i16())
        assertEquals(0x01020304, r.i32())
        assertEquals("héllo", r.string())
        assertEquals(0, r.remaining)
    }

    @Test
    fun shortMessagesAreErrors() {
        assertFailsWith<ProtocolException> { MessageReader(byteArrayOf(0, 1)).i32() }
    }

    @Test
    fun framesPackets() {
        val out = ByteArrayOutputStream()
        PacketStream(ByteArray(0).inputStream(), out).write(byteArrayOf(9, 8, 7))
        assertContentEquals(byteArrayOf(0, 0, 0, 3, 9, 8, 7), out.toByteArray())
        val back = PacketStream(out.toByteArray().inputStream(), ByteArrayOutputStream()).read()
        assertContentEquals(byteArrayOf(9, 8, 7), back)
    }

    @Test
    fun refusesHugePackets() {
        val header = byteArrayOf(0x7f, 0, 0, 0)
        assertFailsWith<ProtocolException> {
            PacketStream(header.inputStream(), ByteArrayOutputStream()).read()
        }
    }
}

class SessionTest {
    // A scripted main computer on the other end of a pair of pipes.
    private class FakeServer {
        val toClient = PipedOutputStream()
        val fromServer = PipedInputStream(toClient, 1 shl 16)
        val toServer = PipedOutputStream()
        val fromClient = PipedInputStream(toServer, 1 shl 16)
        val server = PacketStream(fromClient, toClient)
        val client = PacketStream(fromServer, toServer)

        fun send(w: MessageWriter) = server.write(w.build())
        fun receive() = MessageReader(server.read())
    }

    private class Recorder : ScreenSink {
        val events = mutableListOf<String>()
        override fun connected() { events += "connected" }
        override fun enter(x: Int, y: Int, modifiers: Int) { events += "enter $x $y" }
        override fun leave() { events += "leave" }
        override fun mouseMove(x: Int, y: Int) { events += "move $x $y" }
        override fun mouseDown(button: Int) { events += "down $button" }
        override fun mouseWheel(dx: Int, dy: Int) { events += "wheel $dx $dy" }
        override fun keyDown(key: Int, modifiers: Int, button: Int) { events += "key $key $modifiers" }
        override fun clipboardText(text: String) { events += "clipboard $text" }
    }

    @Test
    fun handshakeAndInput() {
        val fake = FakeServer()
        val recorder = Recorder()
        val session = ClientSession(fake.client, "tablet", { ScreenShape(1280, 800) }, recorder)
        val runner = thread { session.run() }

        fake.send(MessageWriter("Barrier").u16(1).u16(6))
        val hello = fake.receive()
        hello.literal("Barrier")
        assertEquals(1, hello.i16())
        assertEquals(6, hello.i16())
        assertEquals("tablet", hello.string())

        fake.send(MessageWriter(Protocol.QUERY_INFO))
        val info = fake.receive()
        assertEquals(Protocol.INFO, info.code())
        assertEquals(listOf(0, 0, 1280, 800, 0, 0, 0), List(7) { info.i16() })

        fake.send(MessageWriter(Protocol.INFO_ACK))
        fake.send(MessageWriter(Protocol.RESET_OPTIONS))
        fake.send(MessageWriter(Protocol.SET_OPTIONS).u32(0))
        fake.send(MessageWriter(Protocol.KEEP_ALIVE))
        assertEquals(Protocol.KEEP_ALIVE, fake.receive().code())

        fake.send(MessageWriter(Protocol.ENTER).u16(0).u16(400).u32(1).u16(0))
        fake.send(MessageWriter(Protocol.MOUSE_MOVE).u16(10).u16(20))
        fake.send(MessageWriter(Protocol.MOUSE_DOWN).u8(1))
        fake.send(MessageWriter(Protocol.MOUSE_WHEEL).u16(0).u16(-120))
        fake.send(MessageWriter(Protocol.KEY_DOWN).u16('a'.code).u16(AndroidKeys.SHIFT).u16(38))

        val clip = MessageWriter("").u32(1).u32(Protocol.FORMAT_TEXT.toLong()).string("copied").build()
        fake.send(MessageWriter(Protocol.CLIPBOARD_DATA).u8(0).u32(1).u8(Protocol.CLIPBOARD_START)
                      .string(clip.size.toString()))
        fake.send(MessageWriter(Protocol.CLIPBOARD_DATA).u8(0).u32(1).u8(Protocol.CLIPBOARD_CHUNK)
                      .bytes(clip))
        fake.send(MessageWriter(Protocol.CLIPBOARD_DATA).u8(0).u32(1).u8(Protocol.CLIPBOARD_END).string(""))
        fake.send(MessageWriter(Protocol.LEAVE))
        fake.send(MessageWriter(Protocol.CLOSE))
        runner.join(5000)
        assertFalse(runner.isAlive)

        assertEquals(listOf("connected", "enter 0 400", "move 10 20", "down 1", "wheel 0 -120",
                            "key 97 1", "clipboard copied", "leave"), recorder.events)
    }

    @Test
    fun refusedNameIsExplained() {
        val fake = FakeServer()
        val session = ClientSession(fake.client, "tablet", { ScreenShape(1, 1) }, Recorder())
        var error: Throwable? = null
        val runner = thread { error = runCatching { session.run() }.exceptionOrNull() }
        fake.send(MessageWriter("Barrier").u16(1).u16(6))
        fake.receive()
        fake.send(MessageWriter(Protocol.UNKNOWN))
        runner.join(5000)
        assertTrue(error is SessionEndedException)
        assertTrue(error!!.message!!.contains("Arrange screens"))
    }

    @Test
    fun hangingUpAfterHelloMeansTheNameIsUnknown() {
        val fake = FakeServer()
        val session = ClientSession(fake.client, "tablet", { ScreenShape(1, 1) }, Recorder())
        var error: Throwable? = null
        val runner = thread { error = runCatching { session.run() }.exceptionOrNull() }
        fake.send(MessageWriter("Barrier").u16(1).u16(6))
        fake.receive()
        fake.toClient.close()
        runner.join(5000)
        assertTrue(error is SessionEndedException)
        assertTrue(error!!.message!!.contains("Add a computer"))
    }

    @Test
    fun hangingUpBeforeHelloMeansNotAccepted() {
        val fake = FakeServer()
        val session = ClientSession(fake.client, "tablet", { ScreenShape(1, 1) }, Recorder())
        fake.toClient.close()
        val e = assertFailsWith<SessionEndedException> { session.run() }
        assertEquals(ClientSession.NOT_ACCEPTED, e.message)
    }

    @Test
    fun olderServerIsRefused() {
        val fake = FakeServer()
        val session = ClientSession(fake.client, "tablet", { ScreenShape(1, 1) }, Recorder())
        fake.send(MessageWriter("Barrier").u16(1).u16(5))
        assertFailsWith<SessionEndedException> { session.run() }
    }

    @Test
    fun clipboardTextIsFoundAmongFormats() {
        val html = "<b>x</b>".toByteArray()
        val data = MessageWriter("").u32(2).u32(1).bytes(html).u32(0).string("plain").build()
        assertEquals("plain", ClientSession.textOf(data))
        assertNull(ClientSession.textOf(MessageWriter("").u32(1).u32(1).bytes(html).build()))
    }
}

class FingerprintTest {
    @Test
    fun matchesTheDesktopFormat() {
        val f = Fingerprint(ByteArray(32) { it.toByte() })
        assertEquals("v2:sha256:000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", f.dbLine)
        assertEquals("00:01:02:03:04:05:06:07:\n08:09:0A:0B:0C:0D:0E:0F:\n" +
                     "10:11:12:13:14:15:16:17:\n18:19:1A:1B:1C:1D:1E:1F:", f.display)
        assertEquals(f, Fingerprint.parse(f.dbLine))
        assertNull(Fingerprint.parse("v1:sha1:abcd"))
    }

    @Test
    fun remembersTrustedServers() {
        val dir = Files.createTempDirectory("trust").toFile()
        val trusted = TrustedServers(File(dir, "SSL/Fingerprints/TrustedServers.txt"))
        val f = Fingerprint(ByteArray(32) { 7 })
        assertFalse(trusted.contains(f))
        trusted.add(f)
        trusted.add(f)
        assertTrue(trusted.contains(f))
        assertEquals(1, File(dir, "SSL/Fingerprints/TrustedServers.txt").readLines().size)
        dir.deleteRecursively()
    }
}

class AndroidKeysTest {
    @Test
    fun specialKeys() {
        assertEquals(66, AndroidKeys.specialKeyCode(0xEF0D))   // Return -> ENTER
        assertEquals(131, AndroidKeys.specialKeyCode(0xEFBE))  // F1
        assertEquals(142, AndroidKeys.specialKeyCode(0xEFC9))  // F12
        assertEquals(144, AndroidKeys.specialKeyCode(0xEFB0))  // keypad 0
        assertNull(AndroidKeys.specialKeyCode('a'.code))
    }

    @Test
    fun charactersTypeUnlessAShortcutIsHeld() {
        assertTrue(AndroidKeys.typesCharacter('a'.code, AndroidKeys.SHIFT))
        assertTrue(AndroidKeys.typesCharacter('é'.code, 0))
        assertFalse(AndroidKeys.typesCharacter('c'.code, AndroidKeys.CONTROL))
        assertEquals(31, AndroidKeys.keyCodeForCharacter('c'.code))  // KEYCODE_C
        assertEquals(AndroidKeys.META_CTRL_ON or AndroidKeys.META_SHIFT_ON,
                     AndroidKeys.metaState(AndroidKeys.CONTROL or AndroidKeys.SHIFT))
    }
}
