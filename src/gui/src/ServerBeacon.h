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

#include <QObject>
#include <QString>

class QUdpSocket;

// While this computer shares its keyboard and mouse, answers devices on the
// local network that look for it, so they need not be told its address.
class ServerBeacon : public QObject
{
    Q_OBJECT

public:
    explicit ServerBeacon(QObject* parent = nullptr) : QObject(parent) {}

    // listens on the sharing port; returns false if another program has it
    bool start(const QString& name, quint16 port);
    void stop();

private:
    void answer();

    QUdpSocket* m_Socket = nullptr;
    QString m_Name;
    quint16 m_Port = 0;
};
