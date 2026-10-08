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

// Android draws no pointer for injected mouse events, so this draws one: a
// small window above everything that ignores touches and follows the mouse.
class CursorOverlay(private val context: Context) {
    private val main = Handler(Looper.getMainLooper())
    private val windows = context.getSystemService(WindowManager::class.java)!!
    private val size = (24 * context.resources.displayMetrics.density).toInt()
    private val view = PointerView(context)
    private var shown = false
    private var x = 0
    private var y = 0
    private var visible = false
    private var updatePending = false

    private val params = WindowManager.LayoutParams(
        size, size,
        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
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

    val allowed: Boolean get() = Settings.canDrawOverlays(context)

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
        if (!allowed) return
        // the window starts one pixel past the tip, so the spot being
        // clicked is never under it (Android blocks touches through overlays)
        params.x = x + 1
        params.y = y + 1
        try {
            if (visible && !shown) {
                windows.addView(view, params)
                shown = true
            } else if (!visible && shown) {
                windows.removeView(view)
                shown = false
            } else if (shown) {
                windows.updateViewLayout(view, params)
            }
        } catch (e: RuntimeException) {
            shown = false
        }
    }

    fun close() {
        main.post {
            if (shown) windows.removeView(view)
            shown = false
        }
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
