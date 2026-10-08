/*  GlideKVM -- mouse and keyboard sharing utility

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

#include "../src/NetworkAddress.h"

#include <gtest/gtest.h>

namespace glidekvm {

TEST(NetworkAddressTests, RecognisesVirtualAdapters)
{
    EXPECT_TRUE(is_virtual_adapter("vEthernet (WSL)"));
    EXPECT_TRUE(is_virtual_adapter("vEthernet (Default Switch)"));
    EXPECT_TRUE(is_virtual_adapter("VirtualBox Host-Only Network"));
    EXPECT_TRUE(is_virtual_adapter("docker0"));
    EXPECT_TRUE(is_virtual_adapter("vboxnet0"));
    EXPECT_TRUE(is_virtual_adapter("br-5f2e1c"));
    EXPECT_TRUE(is_virtual_adapter("tun0"));
    EXPECT_FALSE(is_virtual_adapter("Wi-Fi"));
    EXPECT_FALSE(is_virtual_adapter("Ethernet"));
    EXPECT_FALSE(is_virtual_adapter("wlan0"));
    EXPECT_FALSE(is_virtual_adapter("en0"));
    EXPECT_FALSE(is_virtual_adapter("enp3s0"));
}

TEST(NetworkAddressTests, RecognisesVirtualHardwareAddresses)
{
    EXPECT_TRUE(is_virtual_hardware_address("0a:00:27:00:00:0c"));
    EXPECT_TRUE(is_virtual_hardware_address("00:15:5D:01:02:03"));
    EXPECT_FALSE(is_virtual_hardware_address("14:B5:CD:02:DE:18"));
    EXPECT_FALSE(is_virtual_hardware_address(""));
}

TEST(NetworkAddressTests, PrefersTheRoutedAddress)
{
    const QStringList candidates = {"192.168.56.1", "192.168.3.164"};
    EXPECT_EQ(pick_preferred_address(candidates, "192.168.3.164"), "192.168.3.164");
    EXPECT_EQ(pick_preferred_address(candidates, ""), "192.168.56.1");
    EXPECT_EQ(pick_preferred_address({"100.70.1.2", "10.0.0.7"}, ""), "10.0.0.7");
    EXPECT_EQ(pick_preferred_address({}, ""), "");
}

TEST(NetworkAddressTests, AnswersOnlyTheQuestion)
{
    EXPECT_TRUE(is_discovery_question(QByteArray("GlideKVM?\x01", 10)));
    EXPECT_FALSE(is_discovery_question(QByteArray("GlideKVM?")));
    EXPECT_FALSE(is_discovery_question(QByteArray("GlideKVM!\x01", 10)));
    EXPECT_FALSE(is_discovery_question(QByteArray("hello")));

    const QByteArray answer = discovery_answer("PC", 24800);
    EXPECT_EQ(answer, QByteArray("GlideKVM!\x01\x60\xe0\x00\x00\x00\x02PC", 18));
}

} // namespace glidekvm
