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

#include "ServerBeacon.h"

#include "NetworkAddress.h"

#include <QNetworkDatagram>
#include <QUdpSocket>

bool ServerBeacon::start(const QString& name, quint16 port)
{
    if (m_Socket && m_Name == name && m_Port == port) {
        return true;
    }
    stop();
    m_Name = name;
    m_Port = port;
    m_Socket = new QUdpSocket(this);
    if (!m_Socket->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress)) {
        delete m_Socket;
        m_Socket = nullptr;
        return false;
    }
    connect(m_Socket, &QUdpSocket::readyRead, this, &ServerBeacon::answer);
    return true;
}

void ServerBeacon::stop()
{
    delete m_Socket;
    m_Socket = nullptr;
}

void ServerBeacon::answer()
{
    while (m_Socket && m_Socket->hasPendingDatagrams()) {
        const QNetworkDatagram question = m_Socket->receiveDatagram(256);
        if (!question.isValid() || !glidekvm::is_discovery_question(question.data())) {
            continue;
        }
        m_Socket->writeDatagram(question.makeReply(glidekvm::discovery_answer(m_Name, m_Port)));
    }
}
