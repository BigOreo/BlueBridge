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

#include <QStringList>

namespace inputleap {

QString normalize_bluetooth_address(const QString& address)
{
    QString text = address.trimmed();
    if (text.startsWith(QLatin1String("bt:"), Qt::CaseInsensitive)) {
        text = text.mid(3);
    }

    QString digits;
    for (QChar c : text) {
        if (c == ':' || c == '-' || c.isSpace()) {
            continue;
        }
        if (!((c >= '0' && c <= '9') || (c.toLower() >= 'a' && c.toLower() <= 'f'))) {
            return {};
        }
        digits += c.toUpper();
    }
    if (digits.size() != 12) {
        return {};
    }

    QStringList bytes;
    for (int i = 0; i < 12; i += 2) {
        bytes << digits.mid(i, 2);
    }
    return bytes.join(':');
}

} // namespace inputleap
