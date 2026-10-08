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

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import rikka.shizuku.Shizuku

// The one screen: where to connect, what this device still needs, and the
// fingerprint to compare with the main computer's.
class MainActivity : Activity() {
    private lateinit var address: EditText
    private lateinit var name: EditText
    private lateinit var connect: Button
    private lateinit var status: TextView
    private lateinit var mouseRow: Row
    private lateinit var keyboardRow: Row
    private lateinit var fullRow: Row
    private lateinit var pointerRow: Row
    private val prefs by lazy { getSharedPreferences("connection", MODE_PRIVATE) }
    private var askedAbout: String? = null

    private val statusListener: (Status) -> Unit = { showStatus(it) }
    private val shizukuBinder = Shizuku.OnBinderReceivedListener { runOnUiThread { refreshSetup() } }
    private val shizukuDead = Shizuku.OnBinderDeadListener { runOnUiThread { refreshSetup() } }
    private val shizukuPermission = Shizuku.OnRequestPermissionResultListener { _, _ ->
        runOnUiThread { refreshSetup() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Colors.MIST
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
        setContentView(buildScreen())
        Shizuku.addBinderReceivedListenerSticky(shizukuBinder)
        Shizuku.addBinderDeadListener(shizukuDead)
        Shizuku.addRequestPermissionResultListener(shizukuPermission)
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
    }

    override fun onResume() {
        super.onResume()
        refreshSetup()
        ConnectionState.listen(statusListener)
    }

    override fun onPause() {
        ConnectionState.unlisten(statusListener)
        super.onPause()
    }

    override fun onDestroy() {
        Shizuku.removeBinderReceivedListener(shizukuBinder)
        Shizuku.removeBinderDeadListener(shizukuDead)
        Shizuku.removeRequestPermissionResultListener(shizukuPermission)
        super.onDestroy()
    }

    private fun buildScreen(): View {
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(24), dp(20), dp(32))
        }

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(ImageView(this).apply { setImageResource(R.mipmap.ic_launcher) },
                       LinearLayout.LayoutParams(dp(44), dp(44)))
        header.addView(text("GlideKVM", 22f, bold = true).apply { setPadding(dp(10), 0, 0, 0) })
        column.addView(header)

        // connect
        val connectCard = card(column)
        connectCard.addView(text("Connect to your main computer", 20f, bold = true))
        connectCard.addView(label("Main computer's address"))
        address = field("192.168.1.20", prefs.getString("address", "") ?: "",
                        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI)
        connectCard.addView(address)
        connectCard.addView(muted("Shown on the main computer's Home screen, under Ways to connect."))
        connectCard.addView(label("This device's name"))
        name = field("", prefs.getString("name", null) ?: DeviceIdentity.defaultName(this),
                     InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS)
        connectCard.addView(name)
        connectCard.addView(muted("On the main computer, open Arrange screens, click Add a computer and use this name."))
        connect = Button(this).apply {
            isAllCaps = false
            setTextColor(Color.WHITE)
            textSize = 16f
            typeface = Typeface.DEFAULT_BOLD
            background = rounded(Colors.BLUE, 12)
            setOnClickListener { toggleConnection() }
        }
        connectCard.addView(connect, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(52)).apply {
            topMargin = dp(16)
        })
        status = muted("").apply { setPadding(0, dp(10), 0, 0) }
        connectCard.addView(status)

        // what this device still needs
        val setupCard = card(column)
        setupCard.addView(text("Set up this device", 18f, bold = true))
        mouseRow = Row(setupCard, "Let the mouse control it")
        keyboardRow = Row(setupCard, "Let the keyboard type")
        fullRow = Row(setupCard, "Full control (optional)")
        pointerRow = Row(setupCard, "Show the pointer")

        // fingerprint
        val identityCard = card(column)
        identityCard.addView(text("This device's fingerprint", 18f, bold = true))
        identityCard.addView(muted("The first time it connects, your main computer shows a fingerprint. " +
                                   "Check that it matches this one."))
        identityCard.addView(TextView(this).apply {
            text = try {
                DeviceIdentity.fingerprint().display
            } catch (e: Exception) {
                "Not available: ${e.message}"
            }
            typeface = Typeface.MONOSPACE
            textSize = 13f
            setTextColor(Colors.INK)
            setTextIsSelectable(true)
            setPadding(0, dp(10), 0, 0)
        })

        return ScrollView(this).apply {
            setBackgroundColor(Colors.MIST)
            addView(column)
        }
    }

    private fun toggleConnection() {
        val status = ConnectionState.status
        if (status is Status.Connecting || status is Status.Connected || status is Status.Retrying) {
            GlideService.stop(this)
            return
        }
        val target = address.text.toString().trim()
        val device = DeviceIdentity.sanitizeName(name.text.toString())
        if (target.isEmpty()) {
            address.error = "Enter the main computer's address"
            return
        }
        if (device.isEmpty()) {
            name.error = "Use letters, numbers, dots, dashes or underscores"
            return
        }
        name.setText(device)
        prefs.edit().putString("address", target).putString("name", device).apply()
        askedAbout = null
        GlideService.start(this, target, device)
    }

    private fun showStatus(s: Status) {
        val busy = s is Status.Connecting || s is Status.Connected || s is Status.Retrying
        connect.text = if (busy) "Disconnect" else "Connect"
        connect.background = rounded(if (busy) Colors.INK else Colors.BLUE, 12)
        status.text = when (s) {
            is Status.Idle -> ""
            is Status.Connecting -> "Connecting to ${s.host}..."
            is Status.Connected -> "Connected to ${s.host}. Move the mouse off the edge of its screen."
            is Status.Retrying -> "${s.reason} Trying again..."
            is Status.Untrusted -> "Waiting for you to confirm the main computer."
            is Status.Failed -> s.reason
        }
        if (s is Status.Untrusted && askedAbout != s.fingerprint.dbLine) {
            askedAbout = s.fingerprint.dbLine
            confirmServer(s)
        }
    }

    private fun confirmServer(s: Status.Untrusted) {
        val message = TextView(this).apply {
            text = "Check that this matches the fingerprint on ${s.host}, under Security.\n\n" +
                s.fingerprint.display
            setTextColor(Colors.INK)
            setPadding(dp(24), dp(8), dp(24), 0)
            setTextIsSelectable(true)
        }
        AlertDialog.Builder(this)
            .setTitle("Is this your main computer?")
            .setView(message)
            .setPositiveButton("They match, connect") { _, _ ->
                DeviceIdentity.trustedServers(this).add(s.fingerprint)
                val target = if (s.port == io.github.bigoreo.glidekvm.core.DEFAULT_PORT) s.host else "${s.host}:${s.port}"
                GlideService.start(this, target, name.text.toString())
            }
            .setNegativeButton("Cancel") { _, _ -> ConnectionState.set(Status.Idle) }
            .show()
    }

    private fun refreshSetup() {
        val touch = GlideAccessibilityService.isEnabled(this)
        if (touch) {
            mouseRow.done("Ready. Clicks are taps, drags are swipes, the right button is a long press.")
        } else {
            mouseRow.show("Turn on GlideKVM mouse under Accessibility. It plays the mouse as taps and swipes, " +
                          "and reads nothing on the screen.", "Open Accessibility") {
                startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            }
        }

        when {
            GlideKeyboard.isEnabled(this) -> keyboardRow.done(
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && touch) {
                    "Ready. It takes over while the mouse is on this device, then your usual keyboard comes back."
                } else {
                    "Ready. The first time you type from the main computer, pick it in the list that appears. " +
                        "Your usual keyboard comes back when the mouse leaves."
                }
            )
            else -> keyboardRow.show(
                "Turn on GlideKVM keyboard. It types what your main computer's keyboard sends.", "Open Keyboards"
            ) { startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)) }
        }

        val full = Injector.readiness(this)
        when (full) {
            Injector.Readiness.NOT_INSTALLED -> fullRow.show(
                "For right-click, hover and shortcuts in every app, install Shizuku. It needs Wireless debugging " +
                    "and a tap after each restart.",
                "Get Shizuku"
            ) { open("https://shizuku.rikka.app/download/") }
            Injector.Readiness.NOT_RUNNING -> fullRow.show(
                "Start Shizuku for full control: open it and tap Start (it uses Wireless debugging).",
                "Open Shizuku"
            ) {
                packageManager.getLaunchIntentForPackage(Injector.SHIZUKU_PACKAGE)?.let { startActivity(it) }
            }
            Injector.Readiness.TOO_OLD -> fullRow.show("Update Shizuku to version 11 or newer.", "Get Shizuku") {
                open("https://shizuku.rikka.app/download/")
            }
            Injector.Readiness.NEEDS_PERMISSION -> fullRow.show("Allow GlideKVM to use Shizuku.", "Allow") {
                Shizuku.requestPermission(2)
            }
            Injector.Readiness.READY -> fullRow.done("On. The mouse and keyboard work like real ones.")
        }

        // the accessibility service draws the pointer itself; without it, the
        // app needs permission to appear on top
        val needsOverlay = !touch && full == Injector.Readiness.READY
        pointerRow.visible = needsOverlay
        if (needsOverlay) {
            if (Settings.canDrawOverlays(this)) {
                pointerRow.done("Ready")
            } else {
                pointerRow.show("Android shows no pointer for a shared mouse, so GlideKVM draws one. " +
                                "Allow it to appear on top.", "Allow") {
                    startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName")))
                }
            }
        }
    }

    private fun open(url: String) = startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))

    // One item of the setup list: what it is, what to do, and a button.
    private inner class Row(parent: LinearLayout, title: String) {
        private val heading = text(title, 15f, bold = true).apply { setPadding(0, dp(14), 0, 0) }
        private val detail = muted("")
        private val button = Button(this@MainActivity).apply {
            isAllCaps = false
            setTextColor(Colors.BLUE)
            background = rounded(Colors.BLUE_TINT, 10)
        }

        init {
            parent.addView(heading)
            parent.addView(detail)
            parent.addView(button, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(44)).apply {
                topMargin = dp(8)
            })
        }

        var visible = true
            set(value) {
                field = value
                val v = if (value) View.VISIBLE else View.GONE
                heading.visibility = v
                detail.visibility = v
                if (!value) button.visibility = View.GONE
            }

        fun show(text: String, action: String, onClick: () -> Unit) {
            detail.text = text
            detail.setTextColor(Colors.SLATE)
            button.text = action
            button.visibility = View.VISIBLE
            button.setOnClickListener { onClick() }
        }

        fun done(text: String) {
            detail.text = "✓ $text"
            detail.setTextColor(Colors.GREEN)
            button.visibility = View.GONE
        }
    }

    private fun card(parent: LinearLayout): LinearLayout {
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(20), dp(20), dp(20))
            background = rounded(Color.WHITE, 16, stroke = Colors.LINE)
        }
        parent.addView(card, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                       ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            topMargin = dp(16)
        })
        return card
    }

    private fun text(value: String, size: Float, bold: Boolean = false) = TextView(this).apply {
        text = value
        textSize = size
        setTextColor(Colors.INK)
        if (bold) typeface = Typeface.DEFAULT_BOLD
    }

    private fun label(value: String) = text(value, 14f, bold = true).apply { setPadding(0, dp(16), 0, dp(6)) }

    private fun muted(value: String) = text(value, 13f).apply {
        setTextColor(Colors.SLATE)
        setPadding(0, dp(4), 0, 0)
    }

    private fun field(hint: String, value: String, type: Int) = EditText(this).apply {
        this.hint = hint
        setText(value)
        inputType = type
        isSingleLine = true
        textSize = 16f
        setTextColor(Colors.INK)
        setPadding(dp(14), dp(12), dp(14), dp(12))
        background = rounded(Color.WHITE, 10, stroke = Colors.FIELD)
    }

    private fun rounded(color: Int, radius: Int, stroke: Int? = null) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radius).toFloat()
        if (stroke != null) setStroke(dp(1), stroke)
    }

    private fun dp(value: Int) =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

    private object Colors {
        val BLUE = Color.parseColor("#1F5EFF")
        val BLUE_TINT = Color.parseColor("#E8EFFF")
        val INK = Color.parseColor("#0F1B2D")
        val SLATE = Color.parseColor("#5B6B82")
        val MIST = Color.parseColor("#F4F6F9")
        val LINE = Color.parseColor("#DCE3EC")
        val FIELD = Color.parseColor("#C9D3E0")
        val GREEN = Color.parseColor("#1A7F4B")
    }
}
