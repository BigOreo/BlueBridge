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

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.content.ComponentName
import android.content.Context
import android.graphics.Path
import android.os.SystemClock
import android.provider.Settings
import android.view.accessibility.AccessibilityEvent
import kotlin.math.abs
import kotlin.math.hypot

// The no-setup way to control the device: once turned on under Settings ->
// Accessibility, it plays the mouse as touch gestures. A click is a tap, a
// drag is a swipe, the right button is a long press and the wheel scrolls.
// It also hosts the pointer. All methods run on the main thread.
class GlideAccessibilityService : AccessibilityService() {
    private var pressed = 0
    private var pressTime = 0L
    private var path: Path? = null
    private var startX = 0f
    private var startY = 0f
    private var farthest = 0f
    private var gestureRunning = false
    // wheel notches that arrived while a gesture was still playing
    private var pendingScrollX = 0f
    private var pendingScrollY = 0f
    private var scrollAtX = 0
    private var scrollAtY = 0

    override fun onServiceConnected() {
        instance = this
    }

    override fun onUnbind(intent: android.content.Intent?): Boolean {
        instance = null
        return super.onUnbind(intent)
    }

    override fun onDestroy() {
        instance = null
        super.onDestroy()
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {}

    override fun onInterrupt() {}

    fun press(button: Int, x: Int, y: Int) {
        if (pressed != 0) return
        pressed = button
        pressTime = SystemClock.uptimeMillis()
        startX = x.toFloat()
        startY = y.toFloat()
        farthest = 0f
        path = Path().apply { moveTo(startX, startY) }
    }

    fun moveTo(x: Int, y: Int) {
        val p = path ?: return
        p.lineTo(x.toFloat(), y.toFloat())
        farthest = maxOf(farthest, hypot(x - startX, y - startY))
    }

    // Plays the press as a gesture: Android has no way to hold a finger down
    // while the mouse moves, so a drag plays back when the button is let go.
    fun release(button: Int, x: Int, y: Int) {
        if (button != pressed) return
        val p = path ?: return
        pressed = 0
        path = null
        var duration = (SystemClock.uptimeMillis() - pressTime).coerceIn(1L, MAX_DRAG_MS)
        val stroke = if (farthest < TAP_SLOP) {
            // a click stays on the spot; the right button is a long press
            if (button == RIGHT) duration = maxOf(duration, LONG_PRESS_MS)
            Path().apply { moveTo(startX, startY) }
        } else {
            p.lineTo(x.toFloat(), y.toFloat())
            p
        }
        play(stroke, duration)
    }

    fun scroll(x: Int, y: Int, notchesX: Float, notchesY: Float) {
        pendingScrollX += notchesX
        pendingScrollY += notchesY
        scrollAtX = x
        scrollAtY = y
        if (!gestureRunning) flushScroll()
    }

    fun back() = performGlobalAction(GLOBAL_ACTION_BACK)

    private fun flushScroll() {
        if (pendingScrollX == 0f && pendingScrollY == 0f) return
        val metrics = resources.displayMetrics
        val step = SCROLL_STEP_DP * metrics.density
        // the wheel turned up shows what is above, which a finger does by moving down
        val dx = (pendingScrollX * -step).coerceIn(-metrics.widthPixels / 2f, metrics.widthPixels / 2f)
        val dy = (pendingScrollY * step).coerceIn(-metrics.heightPixels / 2f, metrics.heightPixels / 2f)
        pendingScrollX = 0f
        pendingScrollY = 0f
        val fromX = scrollAtX.toFloat().coerceIn(1f, metrics.widthPixels - 2f)
        val fromY = scrollAtY.toFloat().coerceIn(1f, metrics.heightPixels - 2f)
        val toX = (fromX + dx).coerceIn(1f, metrics.widthPixels - 2f)
        val toY = (fromY + dy).coerceIn(1f, metrics.heightPixels - 2f)
        if (abs(toX - fromX) < 1f && abs(toY - fromY) < 1f) return
        play(Path().apply { moveTo(fromX, fromY); lineTo(toX, toY) }, SCROLL_MS)
    }

    private fun play(stroke: Path, duration: Long) {
        val gesture = GestureDescription.Builder()
            .addStroke(GestureDescription.StrokeDescription(stroke, 0, duration))
            .build()
        gestureRunning = dispatchGesture(gesture, object : GestureResultCallback() {
            override fun onCompleted(gestureDescription: GestureDescription?) = done()
            override fun onCancelled(gestureDescription: GestureDescription?) = done()
        }, null)
    }

    private fun done() {
        gestureRunning = false
        flushScroll()
    }

    companion object {
        @Volatile
        var instance: GlideAccessibilityService? = null
            private set

        const val LEFT = 1
        const val RIGHT = 3
        private const val TAP_SLOP = 12f
        private const val LONG_PRESS_MS = 650L
        private const val MAX_DRAG_MS = 3_000L
        private const val SCROLL_MS = 120L
        private const val SCROLL_STEP_DP = 90f

        fun isEnabled(context: Context): Boolean {
            val ours = ComponentName(context, GlideAccessibilityService::class.java)
            val enabled = Settings.Secure.getString(context.contentResolver,
                                                    Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES) ?: return false
            return enabled.split(':').any { ComponentName.unflattenFromString(it) == ours }
        }
    }
}
