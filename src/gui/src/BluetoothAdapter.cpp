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
#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>

#include <string>
#include <vector>
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

QList<PairedBluetoothDevice> paired_bluetooth_computers()
{
    QList<PairedBluetoothDevice> result;
#if defined(Q_OS_WIN)
    BLUETOOTH_DEVICE_SEARCH_PARAMS params = {};
    params.dwSize = sizeof(params);
    params.fReturnAuthenticated = TRUE;
    params.fReturnRemembered = TRUE;
    params.fReturnConnected = TRUE;
    params.fReturnUnknown = FALSE;
    params.fIssueInquiry = FALSE;
    params.hRadio = nullptr; // all local radios

    BLUETOOTH_DEVICE_INFO info = {};
    info.dwSize = sizeof(info);
    HBLUETOOTH_DEVICE_FIND find = BluetoothFindFirstDevice(&params, &info);
    if (find == nullptr) {
        return result;
    }
    do {
        if (info.fAuthenticated && GET_COD_MAJOR(info.ulClassofDevice) == COD_MAJOR_COMPUTER) {
            PairedBluetoothDevice device;
            device.name = QString::fromWCharArray(info.szName);
            device.address = normalize_bluetooth_address(
                        QString("%1").arg(info.Address.ullLong, 12, 16, QChar('0')));
            if (device.name.isEmpty()) {
                device.name = device.address;
            }
            result << device;
        }
        info.dwSize = sizeof(info);
    } while (BluetoothFindNextDevice(find, &info));
    BluetoothFindDeviceClose(find);
#endif
    return result;
}

bool is_server_running_on(const QString& address)
{
#if defined(Q_OS_WIN)
    // must match the service class the server registers, see
    // s_bluetoothServiceClass in lib/arch/win32/ArchNetworkWinsock.cpp
    static const GUID server_service_class =
        { 0x3bf44e11, 0x0433, 0x4f8f, { 0xa9, 0x78, 0xaa, 0xaa, 0x2c, 0x49, 0x5c, 0x96 } };

    QString normalized = normalize_bluetooth_address(address);
    if (normalized.isEmpty()) {
        return false;
    }

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }

    std::wstring context = (QStringLiteral("(") + normalized + QStringLiteral(")")).toStdWString();
    GUID service_class = server_service_class;

    WSAQUERYSETW query = {};
    query.dwSize = sizeof(query);
    query.dwNameSpace = NS_BTH;
    query.lpServiceClassId = &service_class;
    query.lpszContext = const_cast<LPWSTR>(context.c_str());

    bool found = false;
    HANDLE lookup = nullptr;
    if (WSALookupServiceBeginW(&query, LUP_FLUSHCACHE | LUP_RETURN_ADDR, &lookup) == 0) {
        std::vector<char> buffer(4096);
        DWORD size = static_cast<DWORD>(buffer.size());
        auto* results = reinterpret_cast<WSAQUERYSETW*>(buffer.data());
        found = WSALookupServiceNextW(lookup, LUP_RETURN_ADDR, &size, results) == 0;
        WSALookupServiceEnd(lookup);
    }
    WSACleanup();
    return found;
#else
    (void) address;
    return false;
#endif
}

} // namespace inputleap
