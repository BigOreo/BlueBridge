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

#include "bridge/BridgeProtocol.h"
#include "server/BridgeClientProxy.h"
#include "glidekvm/option_types.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace glidekvm {

TEST(BridgeKeysTests, CharacterKeysGoByPosition)
{
    // on a French keyboard the key where Q sits on a US one types 'a'
    EXPECT_EQ(keyboard_usage('a', 0x10, KeyButtonKind::WindowsScanCode), 0x14);
    EXPECT_EQ(keyboard_usage('a', 0x10 + 8, KeyButtonKind::XKeyCode), 0x14);
    EXPECT_EQ(keyboard_usage('a', 0x0C + 1, KeyButtonKind::MacVirtualKey), 0x14);
    // the key left of 1 and the extra key on ISO keyboards
    EXPECT_EQ(keyboard_usage('`', 0x29, KeyButtonKind::WindowsScanCode), 0x35);
    EXPECT_EQ(keyboard_usage('<', 0x56, KeyButtonKind::WindowsScanCode), 0x64);
}

TEST(BridgeKeysTests, CharactersWithoutAPositionGoAsOnAUSKeyboard)
{
    EXPECT_EQ(keyboard_usage('A', 0, KeyButtonKind::WindowsScanCode), 0x04);
    EXPECT_EQ(keyboard_usage('!', 0, KeyButtonKind::XKeyCode), 0x1E);
    EXPECT_EQ(keyboard_usage('?', 0, KeyButtonKind::MacVirtualKey), 0x38);
    EXPECT_EQ(keyboard_usage(0x00E9, 0, KeyButtonKind::WindowsScanCode), 0);  // é has no US key
}

TEST(BridgeKeysTests, SpecialKeysGoByWhatTheyAre)
{
    // the extended bit marks the arrow keys that share codes with the keypad
    EXPECT_EQ(keyboard_usage(kKeyLeft, 0x14B, KeyButtonKind::WindowsScanCode), 0x50);
    EXPECT_EQ(keyboard_usage(kKeyReturn, 0x1C, KeyButtonKind::WindowsScanCode), 0x28);
    EXPECT_EQ(keyboard_usage(kKeyF5, 0x3F, KeyButtonKind::WindowsScanCode), 0x3E);
    EXPECT_EQ(keyboard_usage(kKeyKP_7, 0x47, KeyButtonKind::WindowsScanCode), 0x5F);
    EXPECT_EQ(keyboard_usage(kKeySuper_L, 0x15B, KeyButtonKind::WindowsScanCode), 0xE3);
    EXPECT_EQ(keyboard_usage(kKeyControl_R, 0x11D, KeyButtonKind::WindowsScanCode), 0xE4);
    EXPECT_EQ(keyboard_usage(kKeyAudioUp, 0x130, KeyButtonKind::WindowsScanCode), 0);
    EXPECT_EQ(media_usage(kKeyAudioUp), 0xE9);
    EXPECT_EQ(media_usage(kKeyAudioPlay), 0xCD);
    EXPECT_EQ(media_usage('a'), 0);
}

TEST(BridgeKeysTests, ModifiersSwapSidesKept)
{
    EXPECT_EQ(modifier_of_usage(0xE4), kKeyModifierIDControl);
    EXPECT_EQ(usage_for_modifier(kKeyModifierIDSuper, true), 0xE7);
    EXPECT_EQ(usage_for_modifier(kKeyModifierIDSuper, false), 0xE3);
    EXPECT_EQ(modifier_of_usage(0x04), kKeyModifierIDNull);
}

TEST(BridgeKeysTests, WheelCountsWholeNotches)
{
    WheelSteps wheel;
    EXPECT_EQ(wheel.add_vertical(120), -1);  // up, away from the person
    EXPECT_EQ(wheel.add_vertical(-60), 0);
    EXPECT_EQ(wheel.add_vertical(-60), 1);
    EXPECT_EQ(wheel.add_horizontal(240), 2);
}

TEST(BridgeProtocolTests, ReadsTheBoard)
{
    BridgeEvent e = parse_bridge_event("@hello glidekvm-bridge 1 0.1.0 8");
    EXPECT_EQ(e.type, BridgeEvent::Hello);
    EXPECT_EQ(e.protocol, 1);
    EXPECT_EQ(e.version, "0.1.0");
    EXPECT_EQ(e.slot_count, 8);

    e = parse_bridge_event("@slot 2 AA:BB:CC:DD:EE:FF connected Oren's iPad");
    EXPECT_EQ(e.type, BridgeEvent::Slot);
    EXPECT_EQ(e.slot, 2);
    EXPECT_EQ(e.address, "AA:BB:CC:DD:EE:FF");
    EXPECT_EQ(e.state, "connected");
    EXPECT_EQ(e.text, "Oren's iPad");

    EXPECT_EQ(parse_bridge_event("@connected 3").slot, 3);
    EXPECT_EQ(parse_bridge_event("@disconnected 3").type, BridgeEvent::Disconnected);
    EXPECT_EQ(parse_bridge_event("@named 1 iPhone").text, "iPhone");
    EXPECT_EQ(parse_bridge_event("@paired 0 11:22:33:44:55:66").type, BridgeEvent::Paired);
    EXPECT_EQ(parse_bridge_event("@pairing-ended").type, BridgeEvent::PairingEnded);
    EXPECT_EQ(parse_bridge_event("I (300) boot: log").type, BridgeEvent::None);
    EXPECT_EQ(parse_bridge_event("@hello something-else 1 0.1 8").type, BridgeEvent::None);
    EXPECT_EQ(parse_bridge_event("@connected").type, BridgeEvent::None);
}

TEST(BridgeProtocolTests, ReadsDeviceSettings)
{
    BridgeDevice device;
    ASSERT_TRUE(parse_bridge_device("2,Oren-iPad,1366x1024,away", device));
    EXPECT_EQ(device.slot, 2);
    EXPECT_EQ(device.name, "Oren-iPad");
    EXPECT_EQ(device.width, 1366);
    EXPECT_EQ(device.height, 1024);
    EXPECT_TRUE(device.away);
    ASSERT_TRUE(parse_bridge_device("0,phone,430x932", device));
    EXPECT_FALSE(device.away);
    EXPECT_FALSE(parse_bridge_device("x,phone,430x932", device));
    EXPECT_FALSE(parse_bridge_device("1,,430x932", device));
    EXPECT_FALSE(parse_bridge_device("1,phone,430", device));
    EXPECT_FALSE(parse_bridge_device("1,phone,430x932,sometimes", device));
}

class BridgeProxyTests : public ::testing::Test {
protected:
    std::vector<std::string> lines;
    BridgeDevice device()
    {
        BridgeDevice d;
        d.slot = 4;
        d.name = "tablet";
        d.width = 1000;
        d.height = 800;
        return d;
    }
    BridgeWriter writer()
    {
        return [this](const std::string& line) { lines.push_back(line); };
    }
};

TEST_F(BridgeProxyTests, EntersByPushingThePointerToTheCorner)
{
    BridgeClientProxy proxy(device(), writer());
    proxy.linked();
    proxy.enter(0, 300, 1, 0, false);
    EXPECT_EQ(lines, (std::vector<std::string>{"target 4", "m -30000 -30000", "m 0 300"}));

    lines.clear();
    proxy.mouseMove(10, 290);
    proxy.mouseDown(kButtonLeft);
    proxy.mouseDown(kButtonRight);
    proxy.mouseUp(kButtonLeft);
    proxy.mouseWheel(0, -240);
    EXPECT_EQ(lines, (std::vector<std::string>{"m 10 -10", "b 1", "b 3", "b 2", "w 2 0"}));

    std::int32_t x, y;
    proxy.getCursorPos(x, y);
    EXPECT_EQ(x, 10);
    EXPECT_EQ(y, 290);

    lines.clear();
    EXPECT_TRUE(proxy.leave());
    EXPECT_EQ(lines, (std::vector<std::string>{"release", "target -1"}));
}

TEST_F(BridgeProxyTests, LetsGoOfTheKeyThatWasPressed)
{
    BridgeClientProxy proxy(device(), writer());
    proxy.linked();
    proxy.enter(0, 0, 1, 0, false);
    lines.clear();

    const KeyButton q = native_key_button_kind() == KeyButtonKind::WindowsScanCode ? 0x10
                        : native_key_button_kind() == KeyButtonKind::XKeyCode     ? 0x10 + 8
                                                                                  : 0x0C + 1;
    proxy.keyDown('a', 0, q);
    proxy.keyUp('a', 0, q);
    proxy.keyDown(kKeyAudioMute, 0, 0x120);
    proxy.keyUp(kKeyAudioMute, 0, 0x120);
    EXPECT_EQ(lines, (std::vector<std::string>{"kd 14", "ku 14", "cd e2", "cu e2"}));
}

TEST_F(BridgeProxyTests, ControlCanActAsCommand)
{
    BridgeClientProxy proxy(device(), writer());
    proxy.linked();
    proxy.enter(0, 0, 1, 0, false);
    proxy.setOptions({kOptionModifierMapForControl, kKeyModifierIDSuper,
                      kOptionModifierMapForSuper, kKeyModifierIDControl});
    lines.clear();
    proxy.keyDown(kKeyControl_L, 0, 0x1D);
    proxy.keyDown(kKeySuper_R, 0, 0x15C);
    proxy.keyUp(kKeyControl_L, 0, 0x1D);
    EXPECT_EQ(lines, (std::vector<std::string>{"kd e3", "kd e4", "ku e3"}));
}

TEST_F(BridgeProxyTests, AwayDevicesComeBackWhenEntered)
{
    BridgeDevice d = device();
    d.away = true;
    BridgeClientProxy proxy(d, writer());
    proxy.enter(5, 6, 1, 0, false);
    // not linked yet: the pointer is placed once the device is back
    EXPECT_EQ(lines, (std::vector<std::string>{"target 4", "allow 4 1"}));
    proxy.linked();
    EXPECT_EQ(lines.back(), "m 5 6");
    lines.clear();
    proxy.leave();
    EXPECT_EQ(lines, (std::vector<std::string>{"release", "target -1", "allow 4 0"}));
}

} // namespace glidekvm
