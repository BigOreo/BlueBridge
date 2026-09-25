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

#include "common/Policy.h"
#include "inputleap/ArgsBase.h"

#include <gtest/gtest.h>
#include <map>

namespace inputleap {

namespace {

MachinePolicy policy_from(const std::map<std::string, std::uint32_t>& values)
{
    return parse_machine_policy([&values](const std::string& name) -> PolicyValue<std::uint32_t> {
        auto it = values.find(name);
        if (it == values.end()) {
            return {};
        }
        return it->second;
    });
}

} // namespace

TEST(PolicyTests, NothingSetMeansUnmanaged)
{
    MachinePolicy policy = policy_from({});
    EXPECT_FALSE(policy.any());
    EXPECT_FALSE(policy.clipboard_sharing_disabled());
    EXPECT_FALSE(policy.file_transfer_disabled());
    EXPECT_FALSE(policy.encryption_required());
    EXPECT_TRUE(policy.network_allowed());
    EXPECT_TRUE(policy.bluetooth_allowed());
    EXPECT_FALSE(policy.lock_settings);
}

TEST(PolicyTests, ReadsEachSetting)
{
    MachinePolicy policy = policy_from({{"ClipboardSharing", 0}, {"FileTransfer", 0},
                                        {"RequireEncryption", 1}, {"LockSettings", 1},
                                        {"AllowedConnectionModes", 2}});
    EXPECT_TRUE(policy.any());
    EXPECT_TRUE(policy.clipboard_sharing_disabled());
    EXPECT_TRUE(policy.file_transfer_disabled());
    EXPECT_TRUE(policy.encryption_required());
    EXPECT_TRUE(policy.lock_settings);
    EXPECT_FALSE(policy.network_allowed());
    EXPECT_TRUE(policy.bluetooth_allowed());
}

TEST(PolicyTests, EnabledValuesDoNotRestrict)
{
    MachinePolicy policy = policy_from({{"ClipboardSharing", 1}, {"FileTransfer", 1},
                                        {"RequireEncryption", 0}, {"AllowedConnectionModes", 3}});
    EXPECT_TRUE(policy.any());
    EXPECT_FALSE(policy.clipboard_sharing_disabled());
    EXPECT_FALSE(policy.file_transfer_disabled());
    EXPECT_FALSE(policy.encryption_required());
    EXPECT_TRUE(policy.network_allowed());
    EXPECT_TRUE(policy.bluetooth_allowed());
}

TEST(PolicyTests, IgnoresOutOfRangeValues)
{
    MachinePolicy policy = policy_from({{"ClipboardSharing", 7}, {"AllowedConnectionModes", 0}});
    EXPECT_FALSE(policy.any());
    EXPECT_TRUE(policy.network_allowed());
    EXPECT_TRUE(policy.bluetooth_allowed());

    policy = policy_from({{"AllowedConnectionModes", 4}});
    EXPECT_FALSE(policy.allowed_connection_modes.has_value());
}

TEST(PolicyTests, RecognizesBluetoothHosts)
{
    EXPECT_TRUE(is_bluetooth_host("bt"));
    EXPECT_TRUE(is_bluetooth_host("BT"));
    EXPECT_TRUE(is_bluetooth_host("bt:00:28:F8:8F:56:C3"));
    EXPECT_FALSE(is_bluetooth_host(""));
    EXPECT_FALSE(is_bluetooth_host("192.168.1.50"));
    EXPECT_FALSE(is_bluetooth_host("btserver"));
}

} // namespace inputleap
