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

// The lines the main computer sends the bridge over USB. One command per
// line, words separated by spaces, numbers in decimal and HID usages in hex:
//
//   hello              -> @hello glidekvm-bridge <protocol> <firmware> <slots>
//   list               -> @slot <n> <address> <connected|away|off> <name>... then @end
//   pair / pair stop   -> @pairing, then @paired <n> <address> or @pairing-ended
//   forget <n>         -> @forgot <n>
//   allow <n> <0|1>    -> lets a device stay connected, or sends it away
//   target <n>         -> where input goes from now on; -1 for nowhere
//   m <dx> <dy>        mouse movement
//   b <mask>           mouse buttons held: 1 left, 2 right, 4 middle, 8 back, 16 forward
//   w <down> <right>   wheel notches
//   kd <usage> / ku <usage>   key down and up (keyboard page, modifiers E0-E7)
//   cd <usage> / cu <usage>   media key down and up (consumer page)
//   release            lets go of every key and button
//
// The bridge also tells, unasked: @connected <n>, @disconnected <n> and
// @named <n> <name>. Lines that don't start with @ are its log.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRIDGE_PROTOCOL_VERSION 1

typedef enum {
    CMD_NONE,
    CMD_INVALID,
    CMD_HELLO,
    CMD_LIST,
    CMD_PAIR,
    CMD_PAIR_STOP,
    CMD_FORGET,
    CMD_ALLOW,
    CMD_TARGET,
    CMD_MOVE,
    CMD_BUTTONS,
    CMD_WHEEL,
    CMD_KEY_DOWN,
    CMD_KEY_UP,
    CMD_MEDIA_DOWN,
    CMD_MEDIA_UP,
    CMD_RELEASE,
} command_type_t;

typedef struct {
    command_type_t type;
    int32_t a;
    int32_t b;
} command_t;

// Reads one line, without its line ending.
command_t parse_command(const char* line);

#ifdef __cplusplus
}
#endif
