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

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.MotionEvent
import io.github.bigoreo.glidekvm.core.AndroidKeys
import io.github.bigoreo.glidekvm.core.ScreenShape
import io.github.bigoreo.glidekvm.core.ScreenSink
import org.lsposed.hiddenapibypass.HiddenApiBypass

// Turns what the main computer sends into Android input: a mouse with a
// pointer, a keyboard, and the clipboard. With Shizuku running it injects a
// real mouse and keyboard; otherwise the accessibility service plays the
// mouse as touches and the GlideKVM keyboard types.
class InputTranslator(
    private val context: Context,
    private val overlay: CursorOverlay,
    private val shape: () -> ScreenShape,
    private val onConnected: () -> Unit,
) : ScreenSink {
    private val main = Handler(Looper.getMainLooper())
    private val characters = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD)
    private var x = 0
    private var y = 0
    private var buttons = 0
    private var downTime = 0L
    // the Android key code each pressed key was sent as, by the server's key button
    private val pressedKeys = HashMap<Int, Int>()

    val position: Pair<Int, Int> get() = x to y

    // full control through Shizuku, or the accessibility service and keyboard
    private val full: Boolean get() = Injector.ready

    override fun connected() = onConnected()

    override fun enter(x: Int, y: Int, modifiers: Int) {
        moveTo(x, y)
    }

    override fun leave() {
        overlay.hide()
        if (!full) {
            buttons = 0
            return
        }
        // let go of anything still held, as the desktop client does
        if (buttons != 0) {
            val now = SystemClock.uptimeMillis()
            buttons = 0
            inject(motion(MotionEvent.ACTION_UP, now))
        }
        for (code in pressedKeys.values) inject(key(KeyEvent.ACTION_UP, code, 0, 0))
        pressedKeys.clear()
    }

    override fun mouseMove(x: Int, y: Int) = moveTo(x, y)

    override fun mouseRelativeMove(dx: Int, dy: Int) = moveTo(x + dx, y + dy)

    private fun moveTo(newX: Int, newY: Int) {
        val s = shape()
        x = newX.coerceIn(0, s.width - 1)
        y = newY.coerceIn(0, s.height - 1)
        overlay.moveTo(x, y)
        if (!full) {
            val (px, py) = x to y
            main.post { GlideAccessibilityService.instance?.moveTo(px, py) }
            return
        }
        val action = if (buttons != 0) MotionEvent.ACTION_MOVE else MotionEvent.ACTION_HOVER_MOVE
        inject(motion(action, SystemClock.uptimeMillis()))
    }

    override fun mouseDown(button: Int) {
        if (!full) {
            val (px, py) = x to y
            main.post {
                val touch = GlideAccessibilityService.instance ?: return@post
                when (button) {
                    GlideAccessibilityService.LEFT, GlideAccessibilityService.RIGHT -> touch.press(button, px, py)
                    4 -> touch.back()
                }
            }
            return
        }
        val flag = buttonFlag(button)
        if (flag == 0 || buttons and flag != 0) return
        val now = SystemClock.uptimeMillis()
        if (buttons == 0) {
            downTime = now
            buttons = flag
            inject(motion(MotionEvent.ACTION_DOWN, now))
        } else {
            buttons = buttons or flag
        }
        inject(motion(MotionEvent.ACTION_BUTTON_PRESS, now, actionButton = flag))
    }

    override fun mouseUp(button: Int) {
        if (!full) {
            val (px, py) = x to y
            main.post { GlideAccessibilityService.instance?.release(button, px, py) }
            return
        }
        val flag = buttonFlag(button)
        if (flag == 0 || buttons and flag == 0) return
        val now = SystemClock.uptimeMillis()
        buttons = buttons and flag.inv()
        inject(motion(MotionEvent.ACTION_BUTTON_RELEASE, now, actionButton = flag))
        if (buttons == 0) {
            inject(motion(MotionEvent.ACTION_UP, now))
            inject(motion(MotionEvent.ACTION_HOVER_MOVE, now))
        }
    }

    override fun mouseWheel(dx: Int, dy: Int) {
        if (!full) {
            val (px, py) = x to y
            main.post { GlideAccessibilityService.instance?.scroll(px, py, dx / 120f, dy / 120f) }
            return
        }
        val now = SystemClock.uptimeMillis()
        inject(motion(MotionEvent.ACTION_SCROLL, now, vscroll = dy / 120f, hscroll = dx / 120f))
    }

    override fun keyDown(key: Int, modifiers: Int, button: Int) {
        // modifiers travel as the meta state of the keys they change
        if (AndroidKeys.isModifier(key)) return
        if (!full) {
            main.post { typeWithoutShizuku(key, modifiers) }
            return
        }
        if (AndroidKeys.typesCharacter(key, modifiers)) {
            typeCharacter(key)
            return
        }
        val code = AndroidKeys.specialKeyCode(key) ?: AndroidKeys.keyCodeForCharacter(key) ?: return
        pressedKeys[button] = code
        inject(key(KeyEvent.ACTION_DOWN, code, AndroidKeys.metaState(modifiers), 0))
    }

    override fun keyRepeat(key: Int, modifiers: Int, count: Int, button: Int) {
        if (AndroidKeys.isModifier(key)) return
        if (!full) {
            main.post { repeat(count.coerceIn(1, 32)) { typeWithoutShizuku(key, modifiers) } }
            return
        }
        if (AndroidKeys.typesCharacter(key, modifiers)) {
            repeat(count.coerceIn(1, 32)) { typeCharacter(key) }
            return
        }
        val code = pressedKeys[button] ?: return
        inject(key(KeyEvent.ACTION_DOWN, code, AndroidKeys.metaState(modifiers), count))
    }

    override fun keyUp(key: Int, modifiers: Int, button: Int) {
        val code = pressedKeys.remove(button) ?: return
        inject(key(KeyEvent.ACTION_UP, code, AndroidKeys.metaState(modifiers), 0))
    }

    override fun clipboardText(text: String) {
        main.post {
            val clipboard = context.getSystemService(ClipboardManager::class.java)!!
            clipboard.setPrimaryClip(ClipData.newPlainText("GlideKVM", text))
        }
    }

    // On the main thread: the GlideKVM keyboard types into the focused field;
    // a few keys also mean something with no field, through accessibility.
    private fun typeWithoutShizuku(key: Int, modifiers: Int) {
        if (key == KEY_ESCAPE) {
            GlideAccessibilityService.instance?.back()
            return
        }
        GlideKeyboard.instance?.keyDown(key, modifiers)
    }

    // Types a character the way this device's keyboard layout would.
    private fun typeCharacter(key: Int) {
        val events = characters.getEvents(charArrayOf(key.toChar())) ?: return
        val now = SystemClock.uptimeMillis()
        for (event in events) {
            val copy = KeyEvent.changeTimeRepeat(event, now, 0)
            copy.source = InputDevice.SOURCE_KEYBOARD
            inject(copy)
        }
    }

    private fun key(action: Int, code: Int, meta: Int, repeat: Int): KeyEvent {
        val now = SystemClock.uptimeMillis()
        return KeyEvent(now, now, action, code, repeat, meta, KeyCharacterMap.VIRTUAL_KEYBOARD, 0,
                        0, InputDevice.SOURCE_KEYBOARD)
    }

    private fun motion(
        action: Int,
        now: Long,
        actionButton: Int = 0,
        vscroll: Float = 0f,
        hscroll: Float = 0f,
    ): MotionEvent {
        val properties = MotionEvent.PointerProperties().apply {
            id = 0
            toolType = MotionEvent.TOOL_TYPE_MOUSE
        }
        val coords = MotionEvent.PointerCoords().apply {
            this.x = this@InputTranslator.x.toFloat()
            this.y = this@InputTranslator.y.toFloat()
            pressure = if (buttons != 0) 1f else 0f
            size = 1f
            setAxisValue(MotionEvent.AXIS_VSCROLL, vscroll)
            setAxisValue(MotionEvent.AXIS_HSCROLL, hscroll)
        }
        val down = if (buttons != 0 || action == MotionEvent.ACTION_UP ||
            action == MotionEvent.ACTION_BUTTON_RELEASE) downTime else now
        val event = MotionEvent.obtain(down, now, action, 1, arrayOf(properties), arrayOf(coords),
                                       0, buttons, 1f, 1f, 0, 0, InputDevice.SOURCE_MOUSE, 0)
        if (actionButton != 0) {
            // which button changed; Android has no public way to set it
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                HiddenApiBypass.invoke(MotionEvent::class.java, event, "setActionButton", actionButton)
            } else {
                MotionEvent::class.java.getMethod("setActionButton", Int::class.javaPrimitiveType)
                    .invoke(event, actionButton)
            }
        }
        return event
    }

    private fun inject(event: android.view.InputEvent) {
        Injector.inject(event)
    }

    private companion object {
        const val KEY_ESCAPE = 0xEF1B
    }

    private fun buttonFlag(button: Int): Int = when (button) {
        1 -> MotionEvent.BUTTON_PRIMARY
        2 -> MotionEvent.BUTTON_TERTIARY
        3 -> MotionEvent.BUTTON_SECONDARY
        4 -> MotionEvent.BUTTON_BACK
        5 -> MotionEvent.BUTTON_FORWARD
        else -> 0
    }
}
