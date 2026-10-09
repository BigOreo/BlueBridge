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
#pragma once

#include "bridge/BridgeProtocol.h"
#include "server/ClientProxy.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace glidekvm {

// Sends lines to the board; shared by the proxies and the board's manager.
using BridgeWriter = std::function<void(const std::string& line)>;

// A phone or tablet reached through the GlideKVM Bridge board: a screen
// whose keyboard and mouse are a Bluetooth keyboard and mouse. A Bluetooth
// mouse only moves relative to where it is, so the pointer is first pushed
// into the top left corner and then moved to where it should enter, and its
// place is estimated from there on.
class BridgeClientProxy : public ClientProxy {
public:
    BridgeClientProxy(const BridgeDevice& device, BridgeWriter writer);
    ~BridgeClientProxy() override;

    int slot() const { return device_.slot; }
    bool away_when_inactive() const { return device_.away; }

    // the device came back on Bluetooth
    void linked();
    // the device left Bluetooth
    void unlinked() { linked_ = false; }

    // called when the proxy is deleted, by the server
    std::function<void(BridgeClientProxy*)> on_deleted;

    // IScreen
    bool getClipboard(ClipboardID id, IClipboard*) const override;
    void getShape(std::int32_t& x, std::int32_t& y, std::int32_t& width,
                  std::int32_t& height) const override;
    void getCursorPos(std::int32_t& x, std::int32_t& y) const override;

    // IClient
    void enter(std::int32_t xAbs, std::int32_t yAbs, std::uint32_t seqNum, KeyModifierMask mask,
               bool forScreensaver) override;
    bool leave() override;
    void setClipboard(ClipboardID, const IClipboard*) override {}
    void grabClipboard(ClipboardID) override {}
    void setClipboardDirty(ClipboardID, bool) override {}
    void keyDown(KeyID, KeyModifierMask, KeyButton) override;
    void keyRepeat(KeyID, KeyModifierMask, std::int32_t count, KeyButton) override {}
    void keyUp(KeyID, KeyModifierMask, KeyButton) override;
    void mouseDown(ButtonID) override;
    void mouseUp(ButtonID) override;
    void mouseMove(std::int32_t xAbs, std::int32_t yAbs) override;
    void mouseRelativeMove(std::int32_t xRel, std::int32_t yRel) override;
    void mouseWheel(std::int32_t xDelta, std::int32_t yDelta) override;
    void screensaver(bool) override {}
    void resetOptions() override;
    void setOptions(const OptionsList& options) override;
    void sendDragInfo(std::uint32_t, const char*, size_t) override {}
    void file_chunk_sending(const FileChunk&) override {}

private:
    void send(const std::string& line);
    void place_pointer();
    std::uint8_t remap_modifier(std::uint8_t usage) const;

    BridgeDevice device_;
    BridgeWriter writer_;
    bool active_ = false;
    bool linked_ = false;
    bool needs_placing_ = false;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::uint8_t buttons_ = 0;
    WheelSteps wheel_;
    // what each key on the main computer pressed on the device, to let go of
    std::map<KeyButton, std::uint8_t> keys_;
    std::map<KeyButton, std::uint16_t> media_;
    // the person's choice of what each modifier acts as on this device
    std::map<KeyModifierID, KeyModifierID> modifier_map_;
};

} // namespace glidekvm
