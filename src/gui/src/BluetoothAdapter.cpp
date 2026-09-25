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

#include "BluetoothAddress.h"

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bluetoothapis.h>
#endif

namespace inputleap {

bool is_bluetooth_supported()
{
#if defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

QString local_bluetooth_address()
{
#if defined(Q_OS_WIN)
    BLUETOOTH_FIND_RADIO_PARAMS params = {};
    params.dwSize = sizeof(params);
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&params, &radio);
    if (find == nullptr) {
        return {};
    }

    QString result;
    BLUETOOTH_RADIO_INFO info = {};
    info.dwSize = sizeof(info);
    if (BluetoothGetRadioInfo(radio, &info) == ERROR_SUCCESS) {
        result = normalize_bluetooth_address(
                    QString("%1").arg(info.address.ullLong, 12, 16, QChar('0')));
    }
    CloseHandle(radio);
    BluetoothFindRadioClose(find);
    return result;
#else
    return {};
#endif
}

} // namespace inputleap
