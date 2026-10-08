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

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.inputmethod.InputMethodManager

// Keeps the GlideKVM keyboard on only while the main computer's mouse is on
// this device, so the usual keyboard is there whenever the person picks it up.
// Android lets a keyboard hand back to the previous one by itself; switching
// to it needs Android 13's accessibility keyboard control, otherwise the
// person picks it once per visit. Not needed with Shizuku, which types
// directly. All methods run on the main thread.
object KeyboardSwitcher {
    // the keyboard to go back to, when this switched to GlideKVM's
    private var previous: String? = null
    // whether the keyboard list was offered during this visit
    private var offered = false

    fun entered(context: Context) {
        offered = false
        if (Injector.ready || !GlideKeyboard.isEnabled(context) || GlideKeyboard.isChosen(context)) return
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
        val touch = GlideAccessibilityService.instance ?: return
        val current = Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        if (touch.switchKeyboard(GlideKeyboard.id(context))) previous = current
    }

    fun left(context: Context) {
        val back = previous
        previous = null
        if (!GlideKeyboard.isChosen(context)) return
        if (back != null && GlideAccessibilityService.instance?.switchKeyboard(back) == true) return
        GlideKeyboard.instance?.switchBack()
    }

    // A key arrived but the GlideKVM keyboard isn't the one in use: offer the
    // list of keyboards, once per visit.
    fun typedWithoutKeyboard(context: Context) {
        if (offered || !GlideKeyboard.isEnabled(context)) return
        offered = true
        try {
            context.startActivity(
                Intent(context, KeyboardPickerActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_ANIMATION)
            )
        } catch (e: RuntimeException) {
            // Android may refuse to open it from the background; the setup
            // list on the app's screen still explains how
        }
    }
}

// Shows the system's keyboard list over whatever is on screen, then goes away.
// Android only shows that list for an app in front, hence an activity.
class KeyboardPickerActivity : Activity() {
    private var shown = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        overridePendingTransition(0, 0)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus || shown) return
        shown = true
        getSystemService(InputMethodManager::class.java)?.showInputMethodPicker()
        Handler(Looper.getMainLooper()).postDelayed({ finish() }, 500)
    }
}
