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

#pragma once

#include <QList>
#include <QString>

namespace inputleap {

// Returns address in the canonical "XX:XX:XX:XX:XX:XX" form, or an empty
// string if it is not a valid Bluetooth address. Accepts ':', '-' and
// whitespace as separators, or none at all, and an optional "bt:" prefix.
QString normalize_bluetooth_address(const QString& address);

// Returns the address of the first local Bluetooth adapter in canonical form,
// or an empty string if there is none, it is turned off, or Bluetooth is not
// supported on this platform.
QString local_bluetooth_address();

// Whether connecting over Bluetooth is supported on this platform.
bool is_bluetooth_supported();

struct PairedBluetoothDevice {
    QString name;
    QString address; // canonical "XX:XX:XX:XX:XX:XX"
};

// Computers this one is paired with over Bluetooth, as Windows remembers
// them. Does not use the radio, so it returns immediately.
QList<PairedBluetoothDevice> paired_bluetooth_computers();

// Asks the device at address whether it is running the server, by looking up
// the server's Bluetooth service record. Blocks for up to several seconds
// (longer if the device is out of range), so call it off the GUI thread.
bool is_server_running_on(const QString& address);

} // namespace inputleap
