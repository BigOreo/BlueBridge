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
// What the main computer and the GlideKVM Bridge board say to each other,
// kept apart from the serial port so it can be tested. The board's side is
// in bridge/firmware/main/commands.h.

#pragma once

#include "glidekvm/key_types.h"
#include "glidekvm/mouse_types.h"

#include <cstdint>
#include <string>

namespace glidekvm {

// Where the server learns its keys from decides what its key buttons mean.
enum class KeyButtonKind {
    WindowsScanCode,  // scan code, plus 0x100 for extended keys
    XKeyCode,         // Linux evdev code plus 8
    MacVirtualKey,    // macOS virtual key code plus 1
};

KeyButtonKind native_key_button_kind();

// The HID keyboard usage for a key, or 0 if a keyboard can't send it.
// Characters are found by where their key sits, so the device's own layout
// applies, as on the main computer; failing that, as on a US keyboard.
std::uint8_t keyboard_usage(KeyID id, KeyButton button, KeyButtonKind kind);

// The HID consumer usage for a media key, or 0 if it isn't one.
std::uint16_t media_usage(KeyID id);

// Which modifier a keyboard usage is (kKeyModifierIDNull if none), and the
// usage of the same side's key for another modifier.
KeyModifierID modifier_of_usage(std::uint8_t usage);
std::uint8_t usage_for_modifier(KeyModifierID modifier, bool right_side);

// The board's mask bit for a mouse button, or 0.
std::uint8_t button_bit(ButtonID button);

// Turns wheel movement, 120 per notch, into whole notches, keeping the rest.
class WheelSteps {
public:
    // returns notches, positive for towards the person
    std::int32_t add_vertical(std::int32_t delta);
    // returns notches, positive for right
    std::int32_t add_horizontal(std::int32_t delta);

private:
    std::int32_t vertical_ = 0;
    std::int32_t horizontal_ = 0;
};

// A line from the board.
struct BridgeEvent {
    enum Type { None, Hello, Ready, Slot, End, Connected, Disconnected, Named, Paired, Pairing,
                PairingEnded, Forgot, Full, Error };
    Type type = None;
    int slot = -1;
    int protocol = 0;
    int slot_count = 0;
    std::string version;
    std::string address;
    std::string state;  // connected, away or off
    std::string text;   // a name or an error
};

BridgeEvent parse_bridge_event(const std::string& line);

// What the server knows about a phone or tablet paired with the GlideKVM
// Bridge board, from its settings.
struct BridgeDevice {
    int slot = -1;
    std::string name;
    std::int32_t width = 1366;
    std::int32_t height = 1024;
    // disconnects from the board while the mouse is elsewhere, so the device
    // shows its own on-screen keyboard again
    bool away = false;
};

// Parses "SLOT,NAME,WIDTHxHEIGHT[,away]"; false if it isn't that.
bool parse_bridge_device(const std::string& text, BridgeDevice& device);

} // namespace glidekvm
