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

#include "NetworkAddress.h"

#include <QNetworkInterface>
#include <QUdpSocket>
#include <QtEndian>

namespace glidekvm {

namespace {

const char kQuestion[] = "GlideKVM?";
const char kAnswer[] = "GlideKVM!";
const int kDiscoveryVersion = 1;

// The local address the system would send from to reach the wider network.
// Connecting a UDP socket only looks up the route; nothing is sent.
QString routed_address()
{
    QUdpSocket socket;
    socket.connectToHost(QHostAddress("192.0.2.1"), 9);
    if (!socket.waitForConnected(500)) {
        return QString();
    }
    const QHostAddress local = socket.localAddress();
    if (local.protocol() != QAbstractSocket::IPv4Protocol || local.isLoopback()) {
        return QString();
    }
    return local.toString();
}

void append_u32(QByteArray& out, quint32 value)
{
    char bytes[4];
    qToBigEndian(value, bytes);
    out.append(bytes, 4);
}

} // namespace

bool is_virtual_adapter(const QString& name)
{
    static const char* const kVirtual[] = {
        "virtualbox", "vboxnet", "vmware", "vmnet", "vethernet", "hyper-v", "wsl",
        "docker", "veth", "virbr", "br-", "loopback", "npcap", "tap", "tun", "utun",
        "zerotier", "tailscale", "hamachi", "vpn", "pseudo",
    };
    const QString lower = name.toLower();
    for (const char* word : kVirtual) {
        const QString w = QString::fromLatin1(word);
        // short names only count at the start, as in "tun0" or "br-5f2e"
        if (w.size() <= 4 ? lower.startsWith(w) : lower.contains(w)) {
            return true;
        }
    }
    return false;
}

bool is_virtual_hardware_address(const QString& mac)
{
    // the makers' prefixes VirtualBox, VMware, Hyper-V and Parallels give their adapters
    static const char* const kVirtual[] = {
        "0A:00:27", "08:00:27", "00:50:56", "00:0C:29", "00:05:69", "00:1C:14",
        "00:15:5D", "00:1C:42",
    };
    const QString upper = mac.toUpper();
    for (const char* prefix : kVirtual) {
        if (upper.startsWith(QLatin1String(prefix))) {
            return true;
        }
    }
    return false;
}

QString pick_preferred_address(const QStringList& candidates, const QString& routed)
{
    if (!routed.isEmpty() && candidates.contains(routed)) {
        return routed;
    }
    for (const QString& prefix : {QStringLiteral("192.168."), QStringLiteral("10."), QStringLiteral("172.")}) {
        for (const QString& address : candidates) {
            if (address.startsWith(prefix)) {
                return address;
            }
        }
    }
    return candidates.isEmpty() ? QString() : candidates.first();
}

LocalAddresses local_addresses()
{
    QStringList candidates;
    QStringList skipped;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& nif : interfaces) {
        const auto flags = nif.flags();
        const bool usable = flags.testFlag(QNetworkInterface::IsUp) && flags.testFlag(QNetworkInterface::IsRunning) &&
                            !flags.testFlag(QNetworkInterface::IsLoopBack) &&
                            nif.type() != QNetworkInterface::Virtual &&
                            !is_virtual_adapter(nif.humanReadableName()) && !is_virtual_adapter(nif.name()) &&
                            !is_virtual_hardware_address(nif.hardwareAddress());
        for (const QNetworkAddressEntry& entry : nif.addressEntries()) {
            const QHostAddress ip = entry.ip();
            // 169.254.x.x is what a computer makes up when no network gave it an address
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback() || ip.isLinkLocal()) {
                continue;
            }
            (usable ? candidates : skipped) << ip.toString();
        }
    }

    // a virtual adapter that wasn't recognised still loses to the routed one,
    // unless that one goes through a VPN
    const QString routed = routed_address();
    if (!routed.isEmpty() && !candidates.contains(routed) && !skipped.contains(routed)) {
        candidates.prepend(routed);
    }

    LocalAddresses result;
    result.preferred = pick_preferred_address(candidates, routed);
    for (const QString& address : candidates) {
        if (address != result.preferred) {
            result.others << address;
        }
    }
    return result;
}

bool is_discovery_question(const QByteArray& packet)
{
    const int length = sizeof(kQuestion) - 1;
    return packet.size() >= length + 1 && packet.startsWith(kQuestion) &&
           static_cast<unsigned char>(packet.at(length)) >= 1;
}

QByteArray discovery_answer(const QString& name, quint16 port)
{
    QByteArray out(kAnswer);
    out.append(static_cast<char>(kDiscoveryVersion));
    char portBytes[2];
    qToBigEndian(port, portBytes);
    out.append(portBytes, 2);
    const QByteArray utf8 = name.toUtf8();
    append_u32(out, static_cast<quint32>(utf8.size()));
    out.append(utf8);
    return out;
}

} // namespace glidekvm
