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
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.WindowManager

// Android draws no pointer for a mouse it doesn't own, so this draws one: a
// small window above everything that ignores touches and follows the mouse.
// The accessibility service hosts it when it's on, with no extra permission;
// otherwise the app does, if it may appear on top of other apps.
class CursorOverlay(private val context: Context) {
    private val main = Handler(Looper.getMainLooper())
    private var x = 0
    private var y = 0
    private var visible = false
    private var updatePending = false

    // the window as it is now: where it lives and the view in it
    private var host: Context? = null
    private var view: View? = null
    private var params: WindowManager.LayoutParams? = null

    // Called from any thread; the window moves at most once per frame.
    fun moveTo(x: Int, y: Int) {
        synchronized(this) {
            this.x = x
            this.y = y
            visible = true
        }
        schedule()
    }

    fun hide() {
        synchronized(this) { visible = false }
        schedule()
    }

    fun close() {
        main.post { remove() }
    }

    private fun schedule() {
        synchronized(this) {
            if (updatePending) return
            updatePending = true
        }
        main.post { update() }
    }

    private fun update() {
        val (x, y, visible) = synchronized(this) {
            updatePending = false
            Triple(x, y, visible)
        }
        val accessibility = GlideAccessibilityService.instance
        val target: Context? = when {
            !visible -> null
            accessibility != null -> accessibility
            Settings.canDrawOverlays(context) -> context
            else -> null
        }
        if (target !== host) {
            remove()
            if (target == null) return
            add(target, if (target === accessibility) TYPE_ACCESSIBILITY else TYPE_APP)
        }
        val p = params ?: return
        // the window starts one pixel past the tip, so the spot being
        // clicked is never under it (Android blocks touches through overlays)
        p.x = x + 1
        p.y = y + 1
        try {
            windowsOf(host!!).updateViewLayout(view, p)
        } catch (e: RuntimeException) {
            remove()
        }
    }

    private fun add(target: Context, type: Int) {
        val size = (24 * target.resources.displayMetrics.density).toInt()
        val p = WindowManager.LayoutParams(
            size, size, type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT,
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            title = "GlideKVM pointer"
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                fitInsetsTypes = 0
            }
        }
        val v = PointerView(target)
        try {
            windowsOf(target).addView(v, p)
        } catch (e: RuntimeException) {
            return
        }
        host = target
        view = v
        params = p
    }

    private fun remove() {
        val h = host ?: return
        try {
            windowsOf(h).removeView(view)
        } catch (e: RuntimeException) {
        }
        host = null
        view = null
        params = null
    }

    private fun windowsOf(c: Context) = c.getSystemService(WindowManager::class.java)!!

    private companion object {
        const val TYPE_APP = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        const val TYPE_ACCESSIBILITY = WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY
    }

    // The same arrow as the desktop pointer, dark with a light edge so it
    // shows on any background.
    private class PointerView(context: Context) : View(context) {
        private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#0F1B2D") }
        private val edge = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            style = Paint.Style.STROKE
            strokeJoin = Paint.Join.ROUND
        }
        private val path = Path()

        override fun onDraw(canvas: Canvas) {
            val k = width / 24f
            path.reset()
            path.moveTo(1f * k, 0f)
            path.lineTo(1f * k, 18f * k)
            path.lineTo(5.5f * k, 14f * k)
            path.lineTo(8.5f * k, 21f * k)
            path.lineTo(11.5f * k, 19.8f * k)
            path.lineTo(8.6f * k, 13f * k)
            path.lineTo(14.5f * k, 13f * k)
            path.close()
            edge.strokeWidth = 1.6f * k
            canvas.drawPath(path, edge)
            canvas.drawPath(path, fill)
        }
    }
}
