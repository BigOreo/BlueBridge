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

// The Bluetooth side of the bridge: one keyboard-and-mouse that up to
// BRIDGE_SLOTS phones and tablets pair with, and input going to one of them.

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_SLOTS 8
#define BRIDGE_FIRMWARE_VERSION "0.1.0"

void bridge_start(void);

void bridge_list(void);
void bridge_pair(bool on);
void bridge_forget(int slot);
void bridge_allow(int slot, bool allowed);
void bridge_target(int slot);

// whether the device input goes to is connected
bool bridge_target_ready(void);
// send to the device input goes to; false if it isn't connected or is busy
bool bridge_send(uint8_t report_id, const uint8_t* data, int size);

// tells the main computer, as one line starting with @ (in main.c)
void bridge_say(const char* format, ...) __attribute__((format(printf, 1, 2)));
