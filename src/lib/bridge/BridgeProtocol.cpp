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
#include "BridgeProtocol.h"

#include <cstdlib>
#include <sstream>
#include <string>

namespace glidekvm {

namespace {

// Windows scan codes and Linux evdev codes agree for the keys that type
// characters, so one table serves both.
std::uint8_t usage_of_scan_code(unsigned code)
{
    static const std::uint8_t table[0x80] = {
        /* 0x00 */ 0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
        /* 0x08 */ 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,
        /* 0x10 */ 0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C,
        /* 0x18 */ 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16,
        /* 0x20 */ 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33,
        /* 0x28 */ 0x34, 0x35, 0xE1, 0x31, 0x1D, 0x1B, 0x06, 0x19,
        /* 0x30 */ 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x55,
        /* 0x38 */ 0xE2, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
        /* 0x40 */ 0x3F, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5F,
        /* 0x48 */ 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59,
        /* 0x50 */ 0x5A, 0x5B, 0x62, 0x63, 0x00, 0x00, 0x64, 0x44,
        /* 0x58 */ 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    return code < 0x80 ? table[code] : 0;
}

std::uint8_t usage_of_mac_key(unsigned vk)
{
    static const std::uint8_t table[0x60] = {
        /* 0x00 */ 0x04, 0x16, 0x07, 0x09, 0x0B, 0x0A, 0x1D, 0x1B,
        /* 0x08 */ 0x06, 0x19, 0x64, 0x05, 0x14, 0x1A, 0x08, 0x15,
        /* 0x10 */ 0x1C, 0x17, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x22,
        /* 0x18 */ 0x2E, 0x26, 0x24, 0x2D, 0x25, 0x27, 0x30, 0x12,
        /* 0x20 */ 0x18, 0x2F, 0x0C, 0x13, 0x28, 0x0F, 0x0D, 0x34,
        /* 0x28 */ 0x0E, 0x33, 0x31, 0x36, 0x38, 0x11, 0x10, 0x37,
        /* 0x30 */ 0x2B, 0x2C, 0x35, 0x2A, 0x00, 0x29, 0x00, 0x00,
        /* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 0x40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 0x48 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 0x50 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* 0x58 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x87, 0x00,
    };
    return vk < 0x60 ? table[vk] : 0;
}

// The key a character is on, on a US keyboard.
std::uint8_t usage_of_character(KeyID c)
{
    if (c >= 'a' && c <= 'z') return static_cast<std::uint8_t>(0x04 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return static_cast<std::uint8_t>(0x04 + (c - 'A'));
    if (c >= '1' && c <= '9') return static_cast<std::uint8_t>(0x1E + (c - '1'));
    switch (c) {
    case '0': case ')': return 0x27;
    case '!': return 0x1E;
    case '@': return 0x1F;
    case '#': return 0x20;
    case '$': return 0x21;
    case '%': return 0x22;
    case '^': return 0x23;
    case '&': return 0x24;
    case '*': return 0x25;
    case '(': return 0x26;
    case ' ': return 0x2C;
    case '-': case '_': return 0x2D;
    case '=': case '+': return 0x2E;
    case '[': case '{': return 0x2F;
    case ']': case '}': return 0x30;
    case '\\': case '|': return 0x31;
    case ';': case ':': return 0x33;
    case '\'': case '"': return 0x34;
    case '`': case '~': return 0x35;
    case ',': case '<': return 0x36;
    case '.': case '>': return 0x37;
    case '/': case '?': return 0x38;
    case '\n': case '\r': return 0x28;
    case '\t': return 0x2B;
    default: return 0;
    }
}

std::uint8_t usage_of_special(KeyID id)
{
    if (id >= kKeyF1 && id <= kKeyF12) return static_cast<std::uint8_t>(0x3A + (id - kKeyF1));
    if (id >= kKeyF13 && id <= kKeyF24) return static_cast<std::uint8_t>(0x68 + (id - kKeyF13));
    if (id >= kKeyKP_1 && id <= kKeyKP_9) return static_cast<std::uint8_t>(0x59 + (id - kKeyKP_1));
    switch (id) {
    case kKeyBackSpace: return 0x2A;
    case kKeyTab: case kKeyLeftTab: return 0x2B;
    case kKeyReturn: case kKeyLinefeed: return 0x28;
    case kKeyPause: case kKeyBreak: return 0x48;
    case kKeyScrollLock: return 0x47;
    case kKeySysReq: case kKeyPrint: return 0x46;
    case kKeyEscape: return 0x29;
    case kKeyDelete: return 0x4C;
    case kKeyHome: return 0x4A;
    case kKeyLeft: return 0x50;
    case kKeyUp: return 0x52;
    case kKeyRight: return 0x4F;
    case kKeyDown: return 0x51;
    case kKeyPageUp: return 0x4B;
    case kKeyPageDown: return 0x4E;
    case kKeyEnd: return 0x4D;
    case kKeyInsert: return 0x49;
    case kKeySelect: return 0x77;
    case kKeyExecute: return 0x74;
    case kKeyUndo: return 0x7A;
    case kKeyRedo: return 0x79;
    case kKeyMenu: return 0x65;
    case kKeyFind: return 0x7E;
    case kKeyCancel: return 0x78;
    case kKeyHelp: return 0x75;
    case kKeyCopy: return 0x7C;
    case kKeyCut: return 0x7B;
    case kKeyPaste: return 0x7D;
    case kKeyNumLock: return 0x53;
    case kKeyCapsLock: return 0x39;
    case kKeyKP_Space: return 0x2C;
    case kKeyKP_Tab: return 0x2B;
    case kKeyKP_Enter: return 0x58;
    case kKeyKP_Home: return 0x5F;
    case kKeyKP_Left: return 0x5C;
    case kKeyKP_Up: return 0x60;
    case kKeyKP_Right: return 0x5E;
    case kKeyKP_Down: return 0x5A;
    case kKeyKP_PageUp: return 0x61;
    case kKeyKP_PageDown: return 0x5B;
    case kKeyKP_End: return 0x59;
    case kKeyKP_Begin: return 0x5D;
    case kKeyKP_Insert: return 0x62;
    case kKeyKP_Delete: return 0x63;
    case kKeyKP_Equal: return 0x67;
    case kKeyKP_Multiply: return 0x55;
    case kKeyKP_Add: return 0x57;
    case kKeyKP_Separator: return 0x85;
    case kKeyKP_Subtract: return 0x56;
    case kKeyKP_Decimal: return 0x63;
    case kKeyKP_Divide: return 0x54;
    case kKeyKP_0: return 0x62;
    case kKeyShift_L: return 0xE1;
    case kKeyShift_R: return 0xE5;
    case kKeyControl_L: return 0xE0;
    case kKeyControl_R: return 0xE4;
    case kKeyAlt_L: return 0xE2;
    case kKeyAlt_R: case kKeyAltGr: return 0xE6;
    case kKeyMeta_L: case kKeySuper_L: return 0xE3;
    case kKeyMeta_R: case kKeySuper_R: return 0xE7;
    case kKeyMuhenkan: return 0x8B;
    case kKeyHenkan: return 0x8A;
    case kKeyHiraganaKatakana: case kKeyKana: return 0x88;
    case kKeyZenkaku: return 0x35;
    case kKeyHangul: return 0x90;
    case kKeyHanja: case kKeyEisuToggle: return 0x91;
    default: return 0;
    }
}

} // namespace

KeyButtonKind native_key_button_kind()
{
#if defined(_WIN32)
    return KeyButtonKind::WindowsScanCode;
#elif defined(__APPLE__)
    return KeyButtonKind::MacVirtualKey;
#else
    return KeyButtonKind::XKeyCode;
#endif
}

std::uint8_t keyboard_usage(KeyID id, KeyButton button, KeyButtonKind kind)
{
    if (const std::uint8_t special = usage_of_special(id)) {
        return special;
    }
    if (media_usage(id) || (id >= 0xE000 && id < 0xF000)) {
        return 0;  // media keys go as such; other special keys can't be sent
    }

    std::uint8_t by_position = 0;
    switch (kind) {
    case KeyButtonKind::WindowsScanCode:
        // extended keys (0x100) are never character keys
        if (button < 0x100) by_position = usage_of_scan_code(button);
        break;
    case KeyButtonKind::XKeyCode:
        if (button >= 8) by_position = usage_of_scan_code(button - 8u);
        break;
    case KeyButtonKind::MacVirtualKey:
        if (button >= 1) by_position = usage_of_mac_key(button - 1u);
        break;
    }
    // a character key, not one that happens to share a code with a modifier
    if (by_position && by_position < 0xE0) {
        return by_position;
    }
    return usage_of_character(id);
}

std::uint16_t media_usage(KeyID id)
{
    switch (id) {
    case kKeyAudioMute: return 0xE2;
    case kKeyAudioDown: return 0xEA;
    case kKeyAudioUp: return 0xE9;
    case kKeyAudioNext: return 0xB5;
    case kKeyAudioPrev: return 0xB6;
    case kKeyAudioStop: return 0xB7;
    case kKeyAudioPlay: return 0xCD;
    case kKeyEject: return 0xB8;
    case kKeyBrightnessDown: return 0x70;
    case kKeyBrightnessUp: return 0x6F;
    case kKeyWWWSearch: return 0x221;
    case kKeyWWWHome: return 0x223;
    case kKeyWWWBack: return 0x224;
    case kKeyWWWForward: return 0x225;
    case kKeyWWWStop: return 0x226;
    case kKeyWWWRefresh: return 0x227;
    case kKeyWWWFavorites: return 0x22A;
    case kKeyAppMail: return 0x18A;
    default: return 0;
    }
}

KeyModifierID modifier_of_usage(std::uint8_t usage)
{
    switch (usage) {
    case 0xE0: case 0xE4: return kKeyModifierIDControl;
    case 0xE1: case 0xE5: return kKeyModifierIDShift;
    case 0xE2: return kKeyModifierIDAlt;
    case 0xE6: return kKeyModifierIDAlt;
    case 0xE3: case 0xE7: return kKeyModifierIDSuper;
    default: return kKeyModifierIDNull;
    }
}

std::uint8_t usage_for_modifier(KeyModifierID modifier, bool right_side)
{
    switch (modifier) {
    case kKeyModifierIDShift: return right_side ? 0xE5 : 0xE1;
    case kKeyModifierIDControl: return right_side ? 0xE4 : 0xE0;
    case kKeyModifierIDAlt: return right_side ? 0xE6 : 0xE2;
    case kKeyModifierIDAltGr: return 0xE6;
    case kKeyModifierIDMeta:
    case kKeyModifierIDSuper: return right_side ? 0xE7 : 0xE3;
    default: return 0;
    }
}

std::uint8_t button_bit(ButtonID button)
{
    switch (button) {
    case kButtonLeft: return 1;
    case kButtonRight: return 2;
    case kButtonMiddle: return 4;
    case kButtonExtra0: return 8;
    case kButtonExtra0 + 1: return 16;
    default: return 0;
    }
}

std::int32_t WheelSteps::add_vertical(std::int32_t delta)
{
    // a positive delta scrolls up, away from the person
    vertical_ -= delta;
    const std::int32_t notches = vertical_ / 120;
    vertical_ -= notches * 120;
    return notches;
}

std::int32_t WheelSteps::add_horizontal(std::int32_t delta)
{
    horizontal_ += delta;
    const std::int32_t notches = horizontal_ / 120;
    horizontal_ -= notches * 120;
    return notches;
}

BridgeEvent parse_bridge_event(const std::string& line)
{
    BridgeEvent event;
    if (line.empty() || line[0] != '@') {
        return event;
    }
    std::istringstream in(line.substr(1));
    std::string word;
    in >> word;
    auto rest = [&in]() {
        std::string text;
        std::getline(in, text);
        const auto start = text.find_first_not_of(' ');
        return start == std::string::npos ? std::string() : text.substr(start);
    };

    if (word == "hello") {
        std::string what;
        if (in >> what >> event.protocol >> event.version >> event.slot_count && what == "glidekvm-bridge") {
            event.type = BridgeEvent::Hello;
        }
    } else if (word == "ready") {
        event.type = BridgeEvent::Ready;
    } else if (word == "slot") {
        if (in >> event.slot >> event.address >> event.state) {
            event.type = BridgeEvent::Slot;
            event.text = rest();
        }
    } else if (word == "end") {
        event.type = BridgeEvent::End;
    } else if (word == "connected" || word == "disconnected" || word == "forgot") {
        if (in >> event.slot) {
            event.type = word == "connected"      ? BridgeEvent::Connected
                         : word == "disconnected" ? BridgeEvent::Disconnected
                                                  : BridgeEvent::Forgot;
        }
    } else if (word == "named") {
        if (in >> event.slot) {
            event.type = BridgeEvent::Named;
            event.text = rest();
        }
    } else if (word == "paired") {
        if (in >> event.slot >> event.address) {
            event.type = BridgeEvent::Paired;
        }
    } else if (word == "pairing") {
        event.type = BridgeEvent::Pairing;
    } else if (word == "pairing-ended") {
        event.type = BridgeEvent::PairingEnded;
    } else if (word == "full") {
        event.type = BridgeEvent::Full;
    } else if (word == "error") {
        event.type = BridgeEvent::Error;
        event.text = rest();
    }
    if (event.slot < -1 || event.slot > 255) {
        event.type = BridgeEvent::None;
    }
    return event;
}

bool parse_bridge_device(const std::string& text, BridgeDevice& device)
{
    std::istringstream in(text);
    std::string slot, name, size, flag;
    if (!std::getline(in, slot, ',') || !std::getline(in, name, ',') || !std::getline(in, size, ',')) {
        return false;
    }
    std::getline(in, flag, ',');
    BridgeDevice result;
    char* end = nullptr;
    result.slot = static_cast<int>(std::strtol(slot.c_str(), &end, 10));
    if (*end || slot.empty() || result.slot < 0 || result.slot > 255 || name.empty()) {
        return false;
    }
    result.name = name;
    int w = 0, h = 0;
    char x = 0;
    std::istringstream dims(size);
    if (!(dims >> w >> x >> h) || x != 'x' || w < 100 || h < 100 || w > 20000 || h > 20000) {
        return false;
    }
    result.width = w;
    result.height = h;
    if (!flag.empty() && flag != "away") {
        return false;
    }
    result.away = flag == "away";
    device = result;
    return true;
}

} // namespace glidekvm
