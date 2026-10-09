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

// The HID reports the bridge sends: what a Bluetooth keyboard and mouse
// send, so phones and tablets need nothing installed. Kept free of the
// Bluetooth stack so the tests run on a computer.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE = 2,
    REPORT_ID_CONSUMER = 3,
};

#define KEYBOARD_REPORT_SIZE 8
#define MOUSE_REPORT_SIZE 7
#define CONSUMER_REPORT_SIZE 2

extern const uint8_t hid_report_map[];
extern const size_t hid_report_map_size;

// The keys held down. Modifiers are usages 0xE0 to 0xE7, as in the HID tables.
typedef struct {
    uint8_t modifiers;
    uint8_t keys[6];
} keyboard_state_t;

// Both return whether the report changed and so needs sending.
bool keyboard_press(keyboard_state_t* state, uint8_t usage);
bool keyboard_release(keyboard_state_t* state, uint8_t usage);
void keyboard_report(const keyboard_state_t* state, uint8_t out[KEYBOARD_REPORT_SIZE]);

void mouse_report(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel, int8_t pan,
                  uint8_t out[MOUSE_REPORT_SIZE]);

void consumer_report(uint16_t usage, uint8_t out[CONSUMER_REPORT_SIZE]);

// Takes up to one report's worth from a pending movement: what is left stays
// in *pending for the next report.
int16_t take_movement(int32_t* pending);
int8_t take_wheel(int32_t* pending);

#ifdef __cplusplus
}
#endif
