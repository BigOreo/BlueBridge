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

#include "server/BridgeClientProxy.h"
#include "bridge/SerialPort.h"
#include "base/EventTarget.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace glidekvm {

class IEventQueue;
class Server;

// Finds the GlideKVM Bridge board on USB, keeps talking to it, and adds the
// phones and tablets paired with it to the server as they connect.
class BridgeManager : public EventTarget {
public:
    // port is a serial port name, or "auto" to look for the board
    BridgeManager(IEventQueue* events, Server* server, const std::string& port,
                  const std::vector<BridgeDevice>& devices);
    ~BridgeManager();

    BridgeManager(const BridgeManager&) = delete;
    BridgeManager& operator=(const BridgeManager&) = delete;

private:
    struct Link {
        std::mutex mutex;
        SerialPort port;
        bool write(const std::string& line);
    };

    void run();
    bool find_board();
    bool open_board(const std::string& path);
    void post(const std::string& line);
    void handle_line(const std::string& line);
    void add_device(int slot, bool linked);
    void remove_device(int slot);
    void remove_all();
    const BridgeDevice* device_in(int slot) const;

    IEventQueue* events_;
    Server* server_;
    std::string port_name_;
    std::vector<BridgeDevice> devices_;
    std::shared_ptr<Link> link_;
    std::map<int, BridgeClientProxy*> proxies_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
    bool told_missing_ = false;
};

} // namespace glidekvm
