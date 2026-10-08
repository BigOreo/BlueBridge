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

#include "SerialPort.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fstream>
#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif
#endif

namespace glidekvm {

SerialPort::~SerialPort()
{
    close();
}

bool is_board_usb_chip(std::uint16_t vendor_id)
{
    return vendor_id == 0x1A86 || vendor_id == 0x303A || vendor_id == 0x10C4 || vendor_id == 0x0403;
}

namespace {

void sort_board_ports_first(std::vector<SerialPortInfo>& ports)
{
    std::stable_sort(ports.begin(), ports.end(), [](const SerialPortInfo& a, const SerialPortInfo& b) {
        return is_board_usb_chip(a.vendor_id) && !is_board_usb_chip(b.vendor_id);
    });
}

} // namespace

#ifdef _WIN32

namespace {

std::string last_error_text()
{
    char text[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0,
                   text, sizeof text - 1, nullptr);
    std::string result(text);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

std::uint16_t hex_after(const std::string& text, const char* key)
{
    const auto at = text.find(key);
    if (at == std::string::npos) {
        return 0;
    }
    return static_cast<std::uint16_t>(std::strtoul(text.substr(at + std::strlen(key), 4).c_str(), nullptr, 16));
}

} // namespace

bool SerialPort::open(const std::string& path, int baud)
{
    close();
    const std::string device = "\\\\.\\" + path;
    HANDLE handle = CreateFileA(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error_ = last_error_text();
        return false;
    }

    DCB dcb;
    std::memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(handle, &dcb);
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    // on most ESP32 boards these lines reset the chip: keep them released
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(handle, &dcb)) {
        error_ = last_error_text();
        CloseHandle(handle);
        return false;
    }

    COMMTIMEOUTS timeouts;
    std::memset(&timeouts, 0, sizeof timeouts);
    // a read returns as soon as anything arrived
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(handle, &timeouts);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    handle_ = handle;
    read_event_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    write_event_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    return true;
}

void SerialPort::close()
{
    if (handle_) {
        CancelIoEx(static_cast<HANDLE>(handle_), nullptr);
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    if (read_event_) {
        CloseHandle(static_cast<HANDLE>(read_event_));
        read_event_ = nullptr;
    }
    if (write_event_) {
        CloseHandle(static_cast<HANDLE>(write_event_));
        write_event_ = nullptr;
    }
}

bool SerialPort::is_open() const
{
    return handle_ != nullptr;
}

int SerialPort::read(char* buffer, std::size_t size, int timeout_ms)
{
    if (!handle_) {
        return -1;
    }
    HANDLE handle = static_cast<HANDLE>(handle_);
    OVERLAPPED overlapped;
    std::memset(&overlapped, 0, sizeof overlapped);
    overlapped.hEvent = static_cast<HANDLE>(read_event_);
    ResetEvent(overlapped.hEvent);

    DWORD got = 0;
    if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &got, &overlapped)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            error_ = last_error_text();
            return -1;
        }
        if (WaitForSingleObject(overlapped.hEvent, static_cast<DWORD>(timeout_ms)) != WAIT_OBJECT_0) {
            CancelIoEx(handle, &overlapped);
        }
        if (!GetOverlappedResult(handle, &overlapped, &got, TRUE)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) {
                return 0;
            }
            error_ = last_error_text();
            return -1;
        }
    }
    return static_cast<int>(got);
}

bool SerialPort::write(const std::string& data)
{
    if (!handle_) {
        return false;
    }
    HANDLE handle = static_cast<HANDLE>(handle_);
    OVERLAPPED overlapped;
    std::memset(&overlapped, 0, sizeof overlapped);
    overlapped.hEvent = static_cast<HANDLE>(write_event_);
    ResetEvent(overlapped.hEvent);

    DWORD written = 0;
    if (!WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, &overlapped)) {
        if (GetLastError() != ERROR_IO_PENDING ||
            !GetOverlappedResult(handle, &overlapped, &written, TRUE)) {
            error_ = last_error_text();
            return false;
        }
    }
    return written == data.size();
}

std::vector<SerialPortInfo> list_serial_ports()
{
    std::vector<SerialPortInfo> ports;
    HDEVINFO devices = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        return ports;
    }
    SP_DEVINFO_DATA info;
    info.cbSize = sizeof info;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
        char text[512] = {0};
        SerialPortInfo port;

        HKEY key = SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key != INVALID_HANDLE_VALUE) {
            DWORD size = sizeof text - 1;
            DWORD type = 0;
            if (RegQueryValueExA(key, "PortName", nullptr, &type, reinterpret_cast<BYTE*>(text), &size) ==
                    ERROR_SUCCESS && type == REG_SZ) {
                port.path = text;
            }
            RegCloseKey(key);
        }
        if (port.path.rfind("COM", 0) != 0) {
            continue;  // printer ports and the like
        }

        std::memset(text, 0, sizeof text);
        if (SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_FRIENDLYNAME, nullptr,
                                              reinterpret_cast<BYTE*>(text), sizeof text - 1, nullptr)) {
            port.description = text;
        }
        std::memset(text, 0, sizeof text);
        if (SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_HARDWAREID, nullptr,
                                              reinterpret_cast<BYTE*>(text), sizeof text - 1, nullptr)) {
            std::string id(text);
            std::transform(id.begin(), id.end(), id.begin(), ::toupper);
            port.vendor_id = hex_after(id, "VID_");
            port.product_id = hex_after(id, "PID_");
        }
        ports.push_back(port);
    }
    SetupDiDestroyDeviceInfoList(devices);
    sort_board_ports_first(ports);
    return ports;
}

#else // POSIX

namespace {

speed_t speed_of(int baud)
{
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return B115200;
    }
}

std::uint16_t read_hex_file(const std::string& path)
{
    std::ifstream in(path);
    std::string text;
    if (!(in >> text)) {
        return 0;
    }
    return static_cast<std::uint16_t>(std::strtoul(text.c_str(), nullptr, 16));
}

std::string read_line_file(const std::string& path)
{
    std::ifstream in(path);
    std::string text;
    std::getline(in, text);
    return text;
}

} // namespace

bool SerialPort::open(const std::string& path, int baud)
{
    close();
    int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        error_ = std::strerror(errno);
        return false;
    }

    termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        error_ = std::strerror(errno);
        ::close(fd);
        return false;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    tio.c_cflag &= ~HUPCL;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
#ifdef __APPLE__
    cfsetspeed(&tio, B115200);
#else
    cfsetispeed(&tio, speed_of(baud));
    cfsetospeed(&tio, speed_of(baud));
#endif
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        error_ = std::strerror(errno);
        ::close(fd);
        return false;
    }
#ifdef __APPLE__
    // macOS sets speeds above the old fixed list separately
    speed_t speed = static_cast<speed_t>(baud);
    ioctl(fd, IOSSIOSPEED, &speed);
#endif
    // on most ESP32 boards these lines reset the chip: keep them released
    int lines = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIC, &lines);
    tcflush(fd, TCIOFLUSH);
    fd_ = fd;
    return true;
}

void SerialPort::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialPort::is_open() const
{
    return fd_ >= 0;
}

int SerialPort::read(char* buffer, std::size_t size, int timeout_ms)
{
    if (fd_ < 0) {
        return -1;
    }
    pollfd p;
    p.fd = fd_;
    p.events = POLLIN;
    p.revents = 0;
    const int ready = ::poll(&p, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR) {
            return 0;
        }
        error_ = std::strerror(errno);
        return -1;
    }
    if (ready == 0) {
        return 0;
    }
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        error_ = "the device went away";
        return -1;
    }
    const ssize_t got = ::read(fd_, buffer, size);
    if (got < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return 0;
        }
        error_ = std::strerror(errno);
        return -1;
    }
    if (got == 0) {
        error_ = "the device went away";
        return -1;
    }
    return static_cast<int>(got);
}

bool SerialPort::write(const std::string& data)
{
    std::size_t done = 0;
    int waits = 0;
    while (fd_ >= 0 && done < data.size()) {
        const ssize_t n = ::write(fd_, data.data() + done, data.size() - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR) && waits++ < 100) {
            pollfd p;
            p.fd = fd_;
            p.events = POLLOUT;
            p.revents = 0;
            ::poll(&p, 1, 10);
            continue;
        }
        error_ = n < 0 ? std::strerror(errno) : "can't write";
        return false;
    }
    return done == data.size();
}

std::vector<SerialPortInfo> list_serial_ports()
{
    std::vector<SerialPortInfo> ports;
#ifdef __APPLE__
    DIR* dir = opendir("/dev");
    if (!dir) {
        return ports;
    }
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("cu.", 0) != 0) {
            continue;
        }
        SerialPortInfo port;
        port.path = "/dev/" + name;
        port.description = name.substr(3);
        // macOS names the common chips' ports after them
        if (name.find("wchusbserial") != std::string::npos) {
            port.vendor_id = 0x1A86;
        } else if (name.find("SLAB_USBtoUART") != std::string::npos) {
            port.vendor_id = 0x10C4;
        } else if (name.find("usbmodem") != std::string::npos) {
            port.vendor_id = 0x303A;
        } else if (name.find("usbserial") == std::string::npos) {
            continue;  // Bluetooth and other built-in ports
        }
        ports.push_back(port);
    }
    closedir(dir);
#else
    DIR* dir = opendir("/sys/class/tty");
    if (!dir) {
        return ports;
    }
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("ttyUSB", 0) != 0 && name.rfind("ttyACM", 0) != 0) {
            continue;
        }
        SerialPortInfo port;
        port.path = "/dev/" + name;
        char resolved[PATH_MAX];
        const std::string device = "/sys/class/tty/" + name + "/device";
        if (realpath(device.c_str(), resolved)) {
            // the USB device is a few folders up from the serial interface
            std::string at = resolved;
            for (int up = 0; up < 4 && !at.empty(); ++up) {
                const std::uint16_t vendor = read_hex_file(at + "/idVendor");
                if (vendor) {
                    port.vendor_id = vendor;
                    port.product_id = read_hex_file(at + "/idProduct");
                    port.description = read_line_file(at + "/product");
                    break;
                }
                at = at.substr(0, at.find_last_of('/'));
            }
        }
        ports.push_back(port);
    }
    closedir(dir);
#endif
    std::sort(ports.begin(), ports.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) { return a.path < b.path; });
    sort_board_ports_first(ports);
    return ports;
}

#endif

} // namespace glidekvm
