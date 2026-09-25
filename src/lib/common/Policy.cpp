/*
    InputLeap -- mouse and keyboard sharing utility
    Copyright (C) InputLeap contributors

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

#include "Policy.h"

#if defined(_WIN32)
#include "win32/winapi.h"
#else
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#endif

namespace inputleap {

namespace {

const std::uint32_t kAllConnectionModes =
        MachinePolicy::kNetwork | MachinePolicy::kBluetooth;

PolicyValue<bool> read_bool(
        const std::function<PolicyValue<std::uint32_t>(const std::string&)>& get_value,
        const std::string& name)
{
    auto value = get_value(name);
    if (!value || *value > 1) {
        return {};
    }
    return *value == 1;
}

} // namespace

bool MachinePolicy::network_allowed() const
{
    return !allowed_connection_modes || (*allowed_connection_modes & kNetwork) != 0;
}

bool MachinePolicy::bluetooth_allowed() const
{
    return !allowed_connection_modes || (*allowed_connection_modes & kBluetooth) != 0;
}

bool MachinePolicy::any() const
{
    return clipboard_sharing || file_transfer || require_encryption ||
           allowed_connection_modes || lock_settings;
}

MachinePolicy parse_machine_policy(
        const std::function<PolicyValue<std::uint32_t>(const std::string&)>& get_value)
{
    MachinePolicy policy;
    policy.clipboard_sharing = read_bool(get_value, "ClipboardSharing");
    policy.file_transfer = read_bool(get_value, "FileTransfer");
    policy.require_encryption = read_bool(get_value, "RequireEncryption");
    policy.lock_settings = read_bool(get_value, "LockSettings").value_or(false);

    // at least one mode must stay allowed, or nothing could ever connect
    auto modes = get_value("AllowedConnectionModes");
    if (modes && *modes != 0 && (*modes & ~kAllConnectionModes) == 0) {
        policy.allowed_connection_modes = *modes;
    }
    return policy;
}

#if defined(_WIN32)

MachinePolicy read_machine_policy()
{
    return parse_machine_policy([](const std::string& name) -> PolicyValue<std::uint32_t> {
        std::wstring wide_name(name.begin(), name.end());
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\InputLeap", wide_name.c_str(),
                         RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
            return {};
        }
        return static_cast<std::uint32_t>(value);
    });
}

#else

MachinePolicy read_machine_policy()
{
    std::map<std::string, std::uint32_t> values;
    std::ifstream file("/etc/input-leap/policy.conf");
    std::string line;
    while (std::getline(file, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        auto trim = [](std::string s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
            size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
                ++start;
            }
            return s.substr(start);
        };
        std::string name = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        char* end = nullptr;
        unsigned long number = std::strtoul(value.c_str(), &end, 10);
        if (!name.empty() && !value.empty() && end && *end == '\0') {
            values[name] = static_cast<std::uint32_t>(number);
        }
    }

    return parse_machine_policy([&values](const std::string& name) -> PolicyValue<std::uint32_t> {
        auto it = values.find(name);
        if (it == values.end()) {
            return {};
        }
        return it->second;
    });
}

#endif

} // namespace inputleap
