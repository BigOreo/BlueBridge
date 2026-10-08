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
#include "server/BridgeClientProxy.h"

#include "server/IClientConnection.h"
#include "glidekvm/option_types.h"
#include "base/Log.h"

#include <cstdio>
#include <sstream>

namespace glidekvm {

namespace {

// The board is not a network connection: nothing goes this way.
class NoConnection : public IClientConnection {
public:
    const EventTarget* get_event_target() override { return nullptr; }
    IStream* get_stream() override { return nullptr; }
    void send_query_info_1_6() override {}
    void send_leave_1_6() override {}
    void send_enter_1_6(std::int32_t, std::int32_t, std::uint32_t, KeyModifierMask) override {}
    void send_key_down_1_6(KeyID, KeyModifierMask, KeyButton) override {}
    void send_key_up_1_6(KeyID, KeyModifierMask, KeyButton) override {}
    void send_key_repeat_1_6(KeyID, KeyModifierMask, std::int32_t, KeyButton) override {}
    void send_mouse_down_1_6(ButtonID) override {}
    void send_mouse_up_1_6(ButtonID) override {}
    void send_mouse_move_1_6(std::int32_t, std::int32_t) override {}
    void send_mouse_relative_move_1_6(std::int32_t, std::int32_t) override {}
    void send_mouse_wheel_1_6(std::int32_t, std::int32_t) override {}
    void send_drag_info_1_6(std::uint32_t, const std::string&) override {}
    void send_screensaver_1_6(bool) override {}
    void send_reset_options_1_6() override {}
    void send_set_options_1_6(const OptionsList&) override {}
    void send_info_ack_1_6() override {}
    void send_keep_alive_1_6() override {}
    void send_close_1_6(const char*) override {}
    void send_clipboard_chunk_1_6(const ClipboardChunk&) override {}
    void send_file_chunk_1_6(const FileChunk&) override {}
    void send_grab_clipboard(ClipboardID) override {}
    void flush() override {}
    void close() override {}
};

std::int32_t clamp_to(std::int32_t value, std::int32_t size)
{
    return value < 0 ? 0 : value >= size ? size - 1 : value;
}

// how far to push the pointer to be sure it reached the corner
const std::int32_t kHomeDistance = 30000;

} // namespace

BridgeClientProxy::BridgeClientProxy(const BridgeDevice& device, BridgeWriter writer) :
    ClientProxy(device.name, std::make_unique<NoConnection>()),
    device_(device),
    writer_(std::move(writer)),
    x_(device.width / 2),
    y_(device.height / 2)
{
}

BridgeClientProxy::~BridgeClientProxy()
{
    if (on_deleted) {
        on_deleted(this);
    }
}

void BridgeClientProxy::send(const std::string& line)
{
    if (writer_) {
        writer_(line);
    }
}

void BridgeClientProxy::linked()
{
    linked_ = true;
    if (active_ && needs_placing_) {
        place_pointer();
    }
}

void BridgeClientProxy::place_pointer()
{
    needs_placing_ = false;
    send("m " + std::to_string(-kHomeDistance) + " " + std::to_string(-kHomeDistance));
    send("m " + std::to_string(x_) + " " + std::to_string(y_));
}

bool BridgeClientProxy::getClipboard(ClipboardID, IClipboard*) const
{
    return false;
}

void BridgeClientProxy::getShape(std::int32_t& x, std::int32_t& y, std::int32_t& width,
                                 std::int32_t& height) const
{
    x = 0;
    y = 0;
    width = device_.width;
    height = device_.height;
}

void BridgeClientProxy::getCursorPos(std::int32_t& x, std::int32_t& y) const
{
    x = x_;
    y = y_;
}

void BridgeClientProxy::enter(std::int32_t xAbs, std::int32_t yAbs, std::uint32_t, KeyModifierMask,
                              bool)
{
    LOG_DEBUG1("send enter to \"%s\" through the bridge, %d,%d", getName().c_str(), xAbs, yAbs);
    active_ = true;
    x_ = xAbs;
    y_ = yAbs;
    buttons_ = 0;
    keys_.clear();
    media_.clear();
    send("target " + std::to_string(device_.slot));
    if (device_.away) {
        send("allow " + std::to_string(device_.slot) + " 1");
    }
    needs_placing_ = true;
    if (linked_) {
        place_pointer();
    }
}

bool BridgeClientProxy::leave()
{
    LOG_DEBUG1("send leave to \"%s\" through the bridge", getName().c_str());
    active_ = false;
    send("release");
    send("target -1");
    if (device_.away) {
        send("allow " + std::to_string(device_.slot) + " 0");
    }
    keys_.clear();
    media_.clear();
    buttons_ = 0;
    return true;
}

std::uint8_t BridgeClientProxy::remap_modifier(std::uint8_t usage) const
{
    const KeyModifierID modifier = modifier_of_usage(usage);
    const auto mapped = modifier_map_.find(modifier);
    if (modifier == kKeyModifierIDNull || mapped == modifier_map_.end()) {
        return usage;
    }
    if (mapped->second == kKeyModifierIDNull) {
        return 0;  // the person turned this modifier off for this device
    }
    return usage_for_modifier(mapped->second, usage >= 0xE4);
}

void BridgeClientProxy::keyDown(KeyID id, KeyModifierMask, KeyButton button)
{
    if (const std::uint16_t media = media_usage(id)) {
        media_[button] = media;
        char line[16];
        std::snprintf(line, sizeof line, "cd %x", media);
        send(line);
        return;
    }
    const std::uint8_t usage = remap_modifier(keyboard_usage(id, button, native_key_button_kind()));
    if (!usage) {
        LOG_DEBUG1("no Bluetooth key for key id 0x%04x, button 0x%04x", id, button);
        return;
    }
    keys_[button] = usage;
    char line[16];
    std::snprintf(line, sizeof line, "kd %02x", usage);
    send(line);
}

void BridgeClientProxy::keyUp(KeyID, KeyModifierMask, KeyButton button)
{
    char line[16];
    const auto media = media_.find(button);
    if (media != media_.end()) {
        std::snprintf(line, sizeof line, "cu %x", media->second);
        media_.erase(media);
        send(line);
        return;
    }
    const auto key = keys_.find(button);
    if (key != keys_.end()) {
        std::snprintf(line, sizeof line, "ku %02x", key->second);
        keys_.erase(key);
        send(line);
    }
}

void BridgeClientProxy::mouseDown(ButtonID button)
{
    buttons_ |= button_bit(button);
    send("b " + std::to_string(buttons_));
}

void BridgeClientProxy::mouseUp(ButtonID button)
{
    buttons_ &= static_cast<std::uint8_t>(~button_bit(button));
    send("b " + std::to_string(buttons_));
}

void BridgeClientProxy::mouseMove(std::int32_t xAbs, std::int32_t yAbs)
{
    const std::int32_t dx = xAbs - x_;
    const std::int32_t dy = yAbs - y_;
    x_ = xAbs;
    y_ = yAbs;
    if (dx || dy) {
        send("m " + std::to_string(dx) + " " + std::to_string(dy));
    }
}

void BridgeClientProxy::mouseRelativeMove(std::int32_t xRel, std::int32_t yRel)
{
    // kept on the screen, as the device keeps its pointer
    x_ = clamp_to(x_ + xRel, device_.width);
    y_ = clamp_to(y_ + yRel, device_.height);
    if (xRel || yRel) {
        send("m " + std::to_string(xRel) + " " + std::to_string(yRel));
    }
}

void BridgeClientProxy::mouseWheel(std::int32_t xDelta, std::int32_t yDelta)
{
    const std::int32_t down = wheel_.add_vertical(yDelta);
    const std::int32_t right = wheel_.add_horizontal(xDelta);
    if (down || right) {
        send("w " + std::to_string(down) + " " + std::to_string(right));
    }
}

void BridgeClientProxy::resetOptions()
{
    modifier_map_.clear();
}

void BridgeClientProxy::setOptions(const OptionsList& options)
{
    for (std::size_t i = 0; i + 1 < options.size(); i += 2) {
        KeyModifierID from = kKeyModifierIDNull;
        switch (options[i]) {
        case kOptionModifierMapForShift: from = kKeyModifierIDShift; break;
        case kOptionModifierMapForControl: from = kKeyModifierIDControl; break;
        case kOptionModifierMapForAlt: from = kKeyModifierIDAlt; break;
        case kOptionModifierMapForAltGr: from = kKeyModifierIDAltGr; break;
        case kOptionModifierMapForMeta: from = kKeyModifierIDMeta; break;
        case kOptionModifierMapForSuper: from = kKeyModifierIDSuper; break;
        default: continue;
        }
        modifier_map_[from] = static_cast<KeyModifierID>(options[i + 1]);
    }
}

} // namespace glidekvm
