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
#include "bridge/BridgeBoard.h"

#include <chrono>
#include <cstdlib>

namespace glidekvm {

bool LineSplitter::next(std::string& line)
{
    const auto end = buffer_.find('\n');
    if (end == std::string::npos) {
        // nothing sane is this long: start afresh
        if (buffer_.size() > 4096) {
            buffer_.clear();
        }
        return false;
    }
    line = buffer_.substr(0, end);
    buffer_.erase(0, end + 1);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

bool open_bridge_board(SerialPort& port, const std::string& path, const std::atomic<bool>& stop,
                       BridgeEvent* hello, const std::function<bool(const std::string&)>& write)
{
    if (!port.open(path, kBridgeBaud)) {
        return false;
    }
    using clock = std::chrono::steady_clock;
    const auto until = clock::now() + std::chrono::milliseconds(2500);
    auto next_hello = clock::now();
    LineSplitter lines;
    char data[256];
    while (!stop && clock::now() < until) {
        if (clock::now() >= next_hello) {
            write("");
            write("hello");
            next_hello += std::chrono::milliseconds(700);
        }
        const int n = port.read(data, sizeof data, 100);
        if (n < 0) {
            break;
        }
        lines.add(data, static_cast<std::size_t>(n));
        std::string line;
        while (lines.next(line)) {
            const BridgeEvent event = parse_bridge_event(line);
            if (event.type == BridgeEvent::Hello) {
                if (hello) {
                    *hello = event;
                }
                return true;
            }
        }
    }
    return false;
}

bool find_bridge_board(SerialPort& port, const std::string& path, const std::atomic<bool>& stop,
                       BridgeEvent* hello, std::string* found,
                       const std::function<bool(const std::string&)>& write)
{
    std::vector<std::string> candidates;
    // a port can be named when the board isn't recognised, or for testing
    const char* named = std::getenv("GLIDEKVM_BRIDGE_PORT");
    if (path != "auto") {
        candidates.push_back(path);
    } else if (named && *named) {
        candidates.push_back(named);
    } else {
        for (const SerialPortInfo& info : list_serial_ports()) {
            // other serial devices aren't sent anything
            if (is_board_usb_chip(info.vendor_id)) {
                candidates.push_back(info.path);
            }
        }
    }
    for (const std::string& candidate : candidates) {
        if (stop) {
            break;
        }
        if (open_bridge_board(port, candidate, stop, hello, write)) {
            if (found) {
                *found = candidate;
            }
            return true;
        }
        port.close();
    }
    return false;
}

} // namespace glidekvm
