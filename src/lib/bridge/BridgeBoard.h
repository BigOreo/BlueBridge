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
#include "bridge/SerialPort.h"

#include <atomic>
#include <functional>
#include <string>

namespace glidekvm {

// The board's serial speed.
const int kBridgeBaud = 921600;

// Opens the port and checks the GlideKVM Bridge answers on it, giving it a
// moment in case opening the port restarted it. On success the port stays
// open and *hello is the board's greeting. write is how to send a line, so
// callers that share the port can hold their own lock.
bool open_bridge_board(SerialPort& port, const std::string& path, const std::atomic<bool>& stop,
                       BridgeEvent* hello,
                       const std::function<bool(const std::string&)>& write);

// Looks for the board on the given port, or on the USB serial ports of
// chips ESP32 boards use when path is "auto" (or on the port named by the
// GLIDEKVM_BRIDGE_PORT environment variable). *found is the port it is on.
bool find_bridge_board(SerialPort& port, const std::string& path, const std::atomic<bool>& stop,
                       BridgeEvent* hello, std::string* found,
                       const std::function<bool(const std::string&)>& write);

// Splits what arrives into lines, dropping line endings.
class LineSplitter {
public:
    void add(const char* data, std::size_t size) { buffer_.append(data, size); }
    bool next(std::string& line);

private:
    std::string buffer_;
};

} // namespace glidekvm
