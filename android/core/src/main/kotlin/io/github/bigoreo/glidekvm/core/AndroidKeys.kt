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

// Translates GlideKVM key ids (src/lib/glidekvm/key_types.h) to Android key
// codes and meta state. The numbers are Android's public KeyEvent constants,
// written out so this stays plain Kotlin that can be tested anywhere.
object AndroidKeys {
    // GlideKVM modifier mask bits
    const val SHIFT = 0x0001
    const val CONTROL = 0x0002
    const val ALT = 0x0004
    const val META = 0x0008
    const val SUPER = 0x0010
    const val ALT_GR = 0x0020
    const val CAPS_LOCK = 0x1000
    const val NUM_LOCK = 0x2000
    const val SCROLL_LOCK = 0x4000

    // Android KeyEvent meta state flags
    const val META_SHIFT_ON = 0x1
    const val META_ALT_ON = 0x2
    const val META_ALT_RIGHT_ON = 0x20
    const val META_CTRL_ON = 0x1000
    const val META_META_ON = 0x10000
    const val META_CAPS_LOCK_ON = 0x100000
    const val META_NUM_LOCK_ON = 0x200000
    const val META_SCROLL_LOCK_ON = 0x400000

    private const val KEYCODE_0 = 7
    private const val KEYCODE_A = 29

    private val special: Map<Int, Int> = mapOf(
        0xEF08 to 67,   // BackSpace -> DEL
        0xEF09 to 61,   // Tab
        0xEF0D to 66,   // Return -> ENTER
        0xEF13 to 121,  // Pause -> BREAK
        0xEF14 to 116,  // Scroll Lock
        0xEF15 to 120,  // SysReq
        0xEF1B to 111,  // Escape
        0xEFFF to 112,  // Delete -> FORWARD_DEL
        0xEF50 to 122,  // Home -> MOVE_HOME
        0xEF51 to 21,   // Left
        0xEF52 to 19,   // Up
        0xEF53 to 22,   // Right
        0xEF54 to 20,   // Down
        0xEF55 to 92,   // Page Up
        0xEF56 to 93,   // Page Down
        0xEF57 to 123,  // End -> MOVE_END
        0xEF61 to 120,  // Print -> SYSRQ
        0xEF63 to 124,  // Insert
        0xEF67 to 82,   // Menu
        0xEF6B to 121,  // Break
        0xEF7F to 143,  // Num Lock
        0xEF8D to 160,  // keypad Enter
        0xEFAA to 155,  // keypad *
        0xEFAB to 157,  // keypad +
        0xEFAC to 159,  // keypad separator -> NUMPAD_COMMA
        0xEFAD to 156,  // keypad -
        0xEFAE to 158,  // keypad .
        0xEFAF to 154,  // keypad /
        0xEFBD to 161,  // keypad =
        0xEFE1 to 59,   // Shift left
        0xEFE2 to 60,   // Shift right
        0xEFE3 to 113,  // Control left
        0xEFE4 to 114,  // Control right
        0xEFE5 to 115,  // Caps Lock
        0xEFE7 to 117,  // Meta left
        0xEFE8 to 118,  // Meta right
        0xEFE9 to 57,   // Alt left
        0xEFEA to 58,   // Alt right
        0xEF7E to 58,   // AltGr -> Alt right
        0xEFEB to 117,  // Super left (Windows key) -> Meta left
        0xEFEC to 118,  // Super right -> Meta right
        0x0020 to 62,   // space
    )

    // Keypad digits and function keys are runs
    private fun runs(key: Int): Int? = when (key) {
        in 0xEFB0..0xEFB9 -> 144 + (key - 0xEFB0)   // keypad 0-9 -> NUMPAD_0
        in 0xEFBE..0xEFC9 -> 131 + (key - 0xEFBE)   // F1-F12
        in 0xEF95..0xEF9F -> keypadNavigation[key - 0xEF95]
        else -> null
    }

    // keypad Home, Left, Up, Right, Down, PageUp, PageDown, End, Begin, Insert, Delete
    private val keypadNavigation = intArrayOf(122, 21, 19, 22, 20, 92, 93, 123, 122, 124, 112)

    // A key with no printable meaning (arrows, Enter, F keys, modifiers...),
    // or null when the key types a character.
    fun specialKeyCode(key: Int): Int? = special[key] ?: runs(key)

    // The key that types this character on a US keyboard, used when a
    // shortcut such as Ctrl+C needs a key code rather than a character.
    fun keyCodeForCharacter(c: Int): Int? = when (c) {
        in 'a'.code..'z'.code -> KEYCODE_A + (c - 'a'.code)
        in 'A'.code..'Z'.code -> KEYCODE_A + (c - 'A'.code)
        in '0'.code..'9'.code -> KEYCODE_0 + (c - '0'.code)
        ' '.code -> 62
        ','.code -> 55
        '.'.code -> 56
        '`'.code -> 68
        '-'.code -> 69
        '='.code -> 70
        '['.code -> 71
        ']'.code -> 72
        '\\'.code -> 73
        ';'.code -> 74
        '\''.code -> 75
        '/'.code -> 76
        else -> null
    }

    fun isModifier(key: Int): Boolean = key in 0xEFE1..0xEFEE || key == 0xEF7E

    fun metaState(mask: Int): Int {
        var meta = 0
        if (mask and SHIFT != 0) meta = meta or META_SHIFT_ON
        if (mask and CONTROL != 0) meta = meta or META_CTRL_ON
        if (mask and ALT != 0) meta = meta or META_ALT_ON
        if (mask and ALT_GR != 0) meta = meta or META_ALT_ON or META_ALT_RIGHT_ON
        if (mask and (META or SUPER) != 0) meta = meta or META_META_ON
        if (mask and CAPS_LOCK != 0) meta = meta or META_CAPS_LOCK_ON
        if (mask and NUM_LOCK != 0) meta = meta or META_NUM_LOCK_ON
        if (mask and SCROLL_LOCK != 0) meta = meta or META_SCROLL_LOCK_ON
        return meta
    }

    // True when the key should be typed as a character: no shortcut
    // modifier is held, so the device's own layout decides what it types.
    fun typesCharacter(key: Int, mask: Int): Boolean =
        specialKeyCode(key) == null && key in 0x20..0xDFFF &&
            mask and (CONTROL or ALT or META or SUPER) == 0
}
