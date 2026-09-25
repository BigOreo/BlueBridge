/*  InputLeap -- mouse and keyboard sharing utility

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

#include "../src/BluetoothAddress.h"

#include <gtest/gtest.h>

namespace inputleap {

TEST(BluetoothAddressTests, AcceptsCommonFormats)
{
    const QString expected = "00:28:F8:8F:56:C3";
    EXPECT_EQ(normalize_bluetooth_address("00:28:F8:8F:56:C3"), expected);
    EXPECT_EQ(normalize_bluetooth_address("00-28-f8-8f-56-c3"), expected);
    EXPECT_EQ(normalize_bluetooth_address("0028F88F56C3"), expected);
    EXPECT_EQ(normalize_bluetooth_address(" 00 28 F8 8F 56 C3 "), expected);
    EXPECT_EQ(normalize_bluetooth_address("bt:00:28:F8:8F:56:C3"), expected);
    EXPECT_EQ(normalize_bluetooth_address("BT:0028f88f56c3"), expected);
}

TEST(BluetoothAddressTests, RejectsInvalid)
{
    EXPECT_EQ(normalize_bluetooth_address(""), QString());
    EXPECT_EQ(normalize_bluetooth_address("00:28:F8:8F:56"), QString());
    EXPECT_EQ(normalize_bluetooth_address("00:28:F8:8F:56:C3:11"), QString());
    EXPECT_EQ(normalize_bluetooth_address("00:28:F8:8F:56:CG"), QString());
    EXPECT_EQ(normalize_bluetooth_address("192.168.1.50"), QString());
}

} // namespace inputleap
