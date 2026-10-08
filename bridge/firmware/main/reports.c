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

#include "reports.h"

#include <string.h>

const uint8_t hid_report_map[] = {
    // keyboard: modifiers, reserved byte, six keys; lights as output
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, REPORT_ID_KEYBOARD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00,
    0x81, 0x00,
    0xC0,

    // mouse: five buttons, 16-bit movement, wheel and sideways wheel
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, REPORT_ID_MOUSE,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02,
    0x81, 0x06,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,

    // media keys: volume, play and the like
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, REPORT_ID_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};

const size_t hid_report_map_size = sizeof(hid_report_map);

static bool is_modifier(uint8_t usage)
{
    return usage >= 0xE0 && usage <= 0xE7;
}

bool keyboard_press(keyboard_state_t* state, uint8_t usage)
{
    if (usage == 0) {
        return false;
    }
    if (is_modifier(usage)) {
        const uint8_t bit = (uint8_t)(1u << (usage - 0xE0));
        if (state->modifiers & bit) {
            return false;
        }
        state->modifiers |= bit;
        return true;
    }
    for (int i = 0; i < 6; ++i) {
        if (state->keys[i] == usage) {
            return false;
        }
    }
    for (int i = 0; i < 6; ++i) {
        if (state->keys[i] == 0) {
            state->keys[i] = usage;
            return true;
        }
    }
    // more than six keys down: drop the oldest, as typing goes on
    memmove(state->keys, state->keys + 1, 5);
    state->keys[5] = usage;
    return true;
}

bool keyboard_release(keyboard_state_t* state, uint8_t usage)
{
    if (is_modifier(usage)) {
        const uint8_t bit = (uint8_t)(1u << (usage - 0xE0));
        if (!(state->modifiers & bit)) {
            return false;
        }
        state->modifiers &= (uint8_t)~bit;
        return true;
    }
    for (int i = 0; i < 6; ++i) {
        if (state->keys[i] == usage && usage != 0) {
            memmove(state->keys + i, state->keys + i + 1, (size_t)(5 - i));
            state->keys[5] = 0;
            return true;
        }
    }
    return false;
}

void keyboard_report(const keyboard_state_t* state, uint8_t out[KEYBOARD_REPORT_SIZE])
{
    out[0] = state->modifiers;
    out[1] = 0;
    memcpy(out + 2, state->keys, 6);
}

void mouse_report(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, int8_t pan,
                  uint8_t out[MOUSE_REPORT_SIZE])
{
    out[0] = buttons & 0x1F;
    out[1] = (uint8_t)(dx & 0xFF);
    out[2] = (uint8_t)((uint16_t)dx >> 8);
    out[3] = (uint8_t)(dy & 0xFF);
    out[4] = (uint8_t)((uint16_t)dy >> 8);
    out[5] = (uint8_t)wheel;
    out[6] = (uint8_t)pan;
}

void consumer_report(uint16_t usage, uint8_t out[CONSUMER_REPORT_SIZE])
{
    out[0] = (uint8_t)(usage & 0xFF);
    out[1] = (uint8_t)(usage >> 8);
}

int16_t take_movement(int32_t* pending)
{
    int32_t part = *pending;
    if (part > 32767) {
        part = 32767;
    } else if (part < -32767) {
        part = -32767;
    }
    *pending -= part;
    return (int16_t)part;
}

int8_t take_wheel(int32_t* pending)
{
    int32_t part = *pending;
    if (part > 127) {
        part = 127;
    } else if (part < -127) {
        part = -127;
    }
    *pending -= part;
    return (int8_t)part;
}
