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

#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QStringList>

namespace glidekvm {

// Whether a network adapter only reaches virtual machines or containers on
// this computer, such as those VirtualBox, Hyper-V, WSL and Docker add.
bool is_virtual_adapter(const QString& name);

// Whether a network adapter's hardware address is one virtual machine
// programs give their adapters, which often have plain names on Windows.
bool is_virtual_hardware_address(const QString& mac);

struct LocalAddresses {
    // the address other computers on the network reach this one at
    QString preferred;
    // other addresses that may work, on other real networks
    QStringList others;
};

// This computer's network addresses, leaving out virtual adapters and
// addresses that only exist when no network gave one out.
LocalAddresses local_addresses();

// Picks the preferred address among the candidates: the one the system uses
// to reach other networks when known, else a home or office address.
QString pick_preferred_address(const QStringList& candidates, const QString& routed);

// Finding main computers on the local network: a device broadcasts a question
// to the sharing port over UDP and every main computer that is sharing
// answers with its name and port.
bool is_discovery_question(const QByteArray& packet);
QByteArray discovery_answer(const QString& name, quint16 port);

} // namespace glidekvm
