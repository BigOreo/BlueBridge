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
#include "server/BridgeManager.h"

#include "server/Server.h"
#include "bridge/BridgeProtocol.h"
#include "base/IEventQueue.h"
#include "base/Log.h"

#include <chrono>

namespace glidekvm {

namespace {

const int kBaud = 921600;

} // namespace

bool BridgeManager::Link::write(const std::string& line)
{
    std::lock_guard<std::mutex> lock(mutex);
    return port.is_open() && port.write(line + "\n");
}

BridgeManager::BridgeManager(IEventQueue* events, Server* server, const std::string& port,
                             const std::vector<BridgeDevice>& devices) :
    events_(events),
    server_(server),
    port_name_(port),
    devices_(devices),
    link_(std::make_shared<Link>())
{
    events_->add_handler(EventType::BRIDGE_LINE, this,
                         [this](const auto& e) { handle_line(e.template get_data_as<std::string>()); });
    thread_ = std::thread([this]() { run(); });
}

BridgeManager::~BridgeManager()
{
    stopping_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    events_->remove_handler(EventType::BRIDGE_LINE, this);
    {
        std::lock_guard<std::mutex> lock(link_->mutex);
        link_->port.close();
    }
    // the server deletes the proxies; they mustn't call back into this
    for (auto& entry : proxies_) {
        entry.second->on_deleted = nullptr;
    }
}

void BridgeManager::post(const std::string& line)
{
    events_->add_event(EventType::BRIDGE_LINE, this, create_event_data<std::string>(line));
}

bool BridgeManager::open_board(const std::string& path)
{
    {
        std::lock_guard<std::mutex> lock(link_->mutex);
        if (!link_->port.open(path, kBaud)) {
            return false;
        }
    }
    // the board may have restarted as the port opened: give it a moment to answer
    std::string buffer;
    char data[256];
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    auto next_hello = std::chrono::steady_clock::now();
    while (!stopping_ && std::chrono::steady_clock::now() < until) {
        if (std::chrono::steady_clock::now() >= next_hello) {
            link_->write("");
            link_->write("hello");
            next_hello += std::chrono::milliseconds(700);
        }
        const int n = link_->port.read(data, sizeof data, 100);
        if (n < 0) {
            break;
        }
        buffer.append(data, static_cast<std::size_t>(n));
        std::size_t end;
        while ((end = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, end);
            buffer.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const BridgeEvent event = parse_bridge_event(line);
            if (event.type == BridgeEvent::Hello) {
                LOG_NOTE("found the GlideKVM Bridge on %s, firmware %s", path.c_str(), event.version.c_str());
                post(line);
                return true;
            }
        }
    }
    std::lock_guard<std::mutex> lock(link_->mutex);
    link_->port.close();
    return false;
}

bool BridgeManager::find_board()
{
    if (port_name_ != "auto") {
        return open_board(port_name_);
    }
    for (const SerialPortInfo& port : list_serial_ports()) {
        // other serial devices aren't sent anything
        if (!is_board_usb_chip(port.vendor_id) || stopping_) {
            continue;
        }
        if (open_board(port.path)) {
            return true;
        }
    }
    return false;
}

// On its own thread: finds the board, then passes on what it says.
void BridgeManager::run()
{
    while (!stopping_) {
        if (!find_board()) {
            if (!told_missing_) {
                LOG_NOTE("the GlideKVM Bridge isn't plugged in; looking for it");
                told_missing_ = true;
            }
            for (int i = 0; i < 30 && !stopping_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        told_missing_ = false;

        std::string buffer;
        char data[256];
        while (!stopping_) {
            const int n = link_->port.read(data, sizeof data, 100);
            if (n < 0) {
                LOG_WARN("lost the GlideKVM Bridge: %s", link_->port.error().c_str());
                {
                    std::lock_guard<std::mutex> lock(link_->mutex);
                    link_->port.close();
                }
                post("");
                break;
            }
            buffer.append(data, static_cast<std::size_t>(n));
            std::size_t end;
            while ((end = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, end);
                buffer.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty() && line[0] == '@') {
                    post(line);
                }
            }
            if (buffer.size() > 4096) {
                buffer.clear();
            }
        }
    }
}

const BridgeDevice* BridgeManager::device_in(int slot) const
{
    for (const BridgeDevice& device : devices_) {
        if (device.slot == slot) {
            return &device;
        }
    }
    return nullptr;
}

void BridgeManager::add_device(int slot, bool linked)
{
    const auto known = proxies_.find(slot);
    if (known != proxies_.end()) {
        if (linked) {
            known->second->linked();
        }
        return;
    }
    const BridgeDevice* device = device_in(slot);
    if (!device) {
        LOG_NOTE("a device paired with the GlideKVM Bridge (place %d) isn't on the desk", slot);
        return;
    }
    std::weak_ptr<Link> link = link_;
    auto* proxy = new BridgeClientProxy(*device, [link](const std::string& line) {
        if (auto l = link.lock()) {
            l->write(line);
        }
    });
    proxy->on_deleted = [this](BridgeClientProxy* gone) {
        const auto at = proxies_.find(gone->slot());
        if (at != proxies_.end() && at->second == gone) {
            proxies_.erase(at);
        }
    };
    proxies_[slot] = proxy;
    if (linked) {
        proxy->linked();
    }
    // the server owns it from here, and deletes it when it disconnects
    server_->adoptClient(proxy);
}

void BridgeManager::remove_device(int slot)
{
    const auto at = proxies_.find(slot);
    if (at == proxies_.end()) {
        return;
    }
    BridgeClientProxy* proxy = at->second;
    proxies_.erase(at);
    proxy->on_deleted = nullptr;
    LOG_NOTE("client \"%s\" has disconnected", proxy->getName().c_str());
    events_->add_event(EventType::CLIENT_PROXY_DISCONNECTED, proxy->get_event_target());
}

void BridgeManager::remove_all()
{
    while (!proxies_.empty()) {
        remove_device(proxies_.begin()->first);
    }
}

void BridgeManager::handle_line(const std::string& line)
{
    if (line.empty()) {
        remove_all();
        return;
    }
    const BridgeEvent event = parse_bridge_event(line);
    switch (event.type) {
    case BridgeEvent::Hello:
        if (event.protocol != 1) {
            LOG_WARN("the GlideKVM Bridge has firmware %s, which this version can't use; update it",
                     event.version.c_str());
        }
        // devices that stay away while not in use are on the desk all the time
        for (const BridgeDevice& device : devices_) {
            link_->write("allow " + std::to_string(device.slot) + (device.away ? " 0" : " 1"));
            if (device.away) {
                add_device(device.slot, false);
            }
        }
        link_->write("target -1");
        link_->write("list");
        break;
    case BridgeEvent::Slot:
        if (event.state == "connected") {
            add_device(event.slot, true);
        }
        break;
    case BridgeEvent::Connected:
        add_device(event.slot, true);
        break;
    case BridgeEvent::Disconnected: {
        const BridgeDevice* device = device_in(event.slot);
        if (device && device->away) {
            const auto at = proxies_.find(event.slot);
            if (at != proxies_.end()) {
                at->second->unlinked();
            }
        } else {
            remove_device(event.slot);
        }
        break;
    }
    case BridgeEvent::Error:
        LOG_WARN("the GlideKVM Bridge says: %s", event.text.c_str());
        break;
    default:
        break;
    }
}

} // namespace glidekvm
