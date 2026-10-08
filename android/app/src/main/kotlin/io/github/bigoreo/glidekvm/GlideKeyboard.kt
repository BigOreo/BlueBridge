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

import android.content.ComponentName
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.inputmethodservice.InputMethodService
import android.os.Build
import android.os.SystemClock
import android.provider.Settings
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import io.github.bigoreo.glidekvm.core.AndroidKeys

// The no-setup way to type on the device: chosen as the keyboard, it types
// what the main computer's keyboard sends into the focused field. In place
// of keys it shows a slim bar with a way back to the usual keyboard.
// All methods run on the main thread.
class GlideKeyboard : InputMethodService() {
    override fun onCreate() {
        super.onCreate()
        instance = this
    }

    override fun onDestroy() {
        if (instance === this) instance = null
        super.onDestroy()
    }

    override fun onEvaluateFullscreenMode() = false

    override fun onCreateInputView(): View {
        val density = resources.displayMetrics.density
        fun dp(v: Int) = (v * density).toInt()
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16), dp(8), dp(8), dp(8))
            setBackgroundColor(Color.parseColor("#F4F6F9"))
        }
        bar.addView(TextView(this).apply {
            text = "Typing from your main computer"
            setTextColor(Color.parseColor("#3A4A61"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
        }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        bar.addView(Button(this).apply {
            text = "Switch keyboard"
            isAllCaps = false
            setTextColor(Color.parseColor("#1F5EFF"))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#E8EFFF"))
                cornerRadius = dp(10).toFloat()
            }
            setOnClickListener { switchAway() }
        }, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, dp(40)))
        return bar
    }

    // Hands back to the keyboard used before this one.
    fun switchBack() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) switchToPreviousInputMethod()
    }

    private fun switchAway() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P && switchToPreviousInputMethod()) return
        getSystemService(InputMethodManager::class.java)?.showInputMethodPicker()
    }

    // Returns false when no text field has focus.
    fun keyDown(key: Int, modifiers: Int): Boolean {
        val connection = currentInputConnection ?: return false
        if (AndroidKeys.typesCharacter(key, modifiers)) {
            return connection.commitText(String(Character.toChars(key)), 1)
        }
        val code = AndroidKeys.specialKeyCode(key) ?: AndroidKeys.keyCodeForCharacter(key) ?: return false
        val shortcut = modifiers and (AndroidKeys.CONTROL or AndroidKeys.ALT or AndroidKeys.META or AndroidKeys.SUPER) != 0
        if (code == KeyEvent.KEYCODE_ENTER && !shortcut && editorAction()?.let { connection.performEditorAction(it) } == true) {
            return true
        }
        // a key with its modifiers, which text fields understand as shortcuts
        // such as Ctrl+C, Ctrl+V, Ctrl+A and Shift+arrows
        val now = SystemClock.uptimeMillis()
        val meta = AndroidKeys.metaState(modifiers)
        for (action in intArrayOf(KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP)) {
            connection.sendKeyEvent(KeyEvent(now, now, action, code, 0, meta, KeyCharacterMap.VIRTUAL_KEYBOARD, 0,
                                             KeyEvent.FLAG_SOFT_KEYBOARD or KeyEvent.FLAG_KEEP_TOUCH_MODE))
        }
        return true
    }

    // What Enter does in a one-line field, such as Search or Send.
    private fun editorAction(): Int? {
        val info = currentInputEditorInfo ?: return null
        if (info.imeOptions and EditorInfo.IME_FLAG_NO_ENTER_ACTION != 0) return null
        if (info.inputType and EditorInfo.TYPE_TEXT_FLAG_MULTI_LINE != 0) return null
        val action = info.imeOptions and EditorInfo.IME_MASK_ACTION
        return if (action == EditorInfo.IME_ACTION_NONE || action == EditorInfo.IME_ACTION_UNSPECIFIED) null else action
    }

    companion object {
        @Volatile
        var instance: GlideKeyboard? = null
            private set

        fun id(context: Context) = ComponentName(context, GlideKeyboard::class.java).flattenToShortString()

        fun isEnabled(context: Context): Boolean =
            context.getSystemService(InputMethodManager::class.java)?.enabledInputMethodList
                ?.any { it.id == id(context) } == true

        fun isChosen(context: Context): Boolean =
            Settings.Secure.getString(context.contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD) == id(context)
    }
}
