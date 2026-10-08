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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace glidekvm {

// A USB serial port, as the GlideKVM Bridge board shows up on the computer.
// Reading and writing may happen on different threads at once.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // opens the port at 8N1 without flow control; false with error() set if it can't
    bool open(const std::string& path, int baud);
    void close();
    bool is_open() const;

    // waits up to timeout_ms for some bytes; returns how many were read,
    // 0 on timeout and -1 when the port went away
    int read(char* buffer, std::size_t size, int timeout_ms);
    bool write(const std::string& data);

    const std::string& error() const { return error_; }

private:
#ifdef _WIN32
    void* handle_ = nullptr;
    void* read_event_ = nullptr;
    void* write_event_ = nullptr;
#else
    int fd_ = -1;
#endif
    std::string error_;
};

struct SerialPortInfo {
    std::string path;          // COM5, /dev/ttyUSB0, /dev/cu.usbserial-110
    std::string description;   // what the system calls it, when known
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
};

// USB serial ports, the ones made by chips that ESP32 boards use first.
std::vector<SerialPortInfo> list_serial_ports();

// Whether the USB ids belong to a chip ESP32 boards use: WCH (CH340, CH343),
// Espressif's own USB, Silicon Labs (CP210x) or FTDI.
bool is_board_usb_chip(std::uint16_t vendor_id);

} // namespace glidekvm
