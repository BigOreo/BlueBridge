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

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace inputleap {

// A policy value that may not be set. Stands in for std::optional, which the
// macOS x86_64 build (C++14) does not have.
template<class T>
class PolicyValue {
public:
    PolicyValue() = default;
    PolicyValue(T value) : value_(value), set_(true) {}

    bool has_value() const { return set_; }
    explicit operator bool() const { return set_; }
    const T& operator*() const { return value_; }
    T value_or(T fallback) const { return set_ ? value_ : fallback; }

    friend bool operator==(const PolicyValue& a, T b) { return a.set_ && a.value_ == b; }

private:
    T value_{};
    bool set_ = false;
};

// Settings an administrator enforces for every user of this computer, set
// through Group Policy, an MDM such as Intune, or a configuration file. A
// value that is not set leaves the user free to choose.
//
// Windows: DWORD values under HKEY_LOCAL_MACHINE\SOFTWARE\Policies\InputLeap
// (writable only by administrators). Other platforms: NAME=VALUE lines in
// /etc/input-leap/policy.conf.
struct MachinePolicy {
    enum ConnectionModes : std::uint32_t {
        kNetwork = 1,
        kBluetooth = 2,
    };

    PolicyValue<bool> clipboard_sharing;                 // ClipboardSharing
    PolicyValue<bool> file_transfer;                     // FileTransfer
    PolicyValue<bool> require_encryption;                // RequireEncryption
    PolicyValue<std::uint32_t> allowed_connection_modes; // AllowedConnectionModes
    bool lock_settings = false;                          // LockSettings

    bool clipboard_sharing_disabled() const { return clipboard_sharing == false; }
    bool file_transfer_disabled() const { return file_transfer == false; }
    bool encryption_required() const { return require_encryption == true; }
    bool network_allowed() const;
    bool bluetooth_allowed() const;

    // Whether any policy is set, i.e. the computer is managed.
    bool any() const;
};

// Builds a policy from named numeric values; get_value returns nothing for a
// value that is not set. Values that are out of range are ignored.
MachinePolicy parse_machine_policy(
        const std::function<PolicyValue<std::uint32_t>(const std::string&)>& get_value);

// Reads the policy that applies to this computer.
MachinePolicy read_machine_policy();

} // namespace inputleap
