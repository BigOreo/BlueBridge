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

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace glidekvm {
class SerialPort;
}

// Talks to the GlideKVM Bridge board on a thread of its own and hands what
// it says to the GUI thread.
class BridgeConnection : public QObject
{
    Q_OBJECT

public:
    explicit BridgeConnection(QObject* parent = nullptr);
    ~BridgeConnection() override;

    // looks for the board for up to a few seconds, as sharing may still be
    // letting go of it
    void start();
    void stop();
    bool send(const QString& line);

signals:
    void found(const QString& port, const QString& version);
    void notFound();
    void lineReceived(const QString& line);
    void lost();

private:
    void run();

    std::thread m_Thread;
    std::atomic<bool> m_Stopping{false};
    std::mutex m_Mutex;
    std::unique_ptr<glidekvm::SerialPort> m_Port;
};

// Pairs phones and tablets with the GlideKVM Bridge board, and forgets them.
class BridgeDialog : public QDialog
{
    Q_OBJECT

public:
    struct Device {
        int slot = -1;
        QString name;      // what the device calls itself
        QString address;
        bool connected = false;
    };

    // onDesk: the devices already on the desk, by slot, with their names there
    BridgeDialog(QWidget* parent, const QMap<int, QString>& onDesk);

    // paired devices to put on the desk, and the slots of forgotten ones
    const QList<Device>& toAdd() const { return m_ToAdd; }
    const QList<int>& forgotten() const { return m_Forgotten; }

private:
    void lookForBoard();
    void handleLine(const QString& line);
    void startPairing();
    void stopPairing();
    void forget(int slot);
    void refreshList();
    void showStatus(const QString& text);

    BridgeConnection* m_pConnection;
    QMap<int, QString> m_OnDesk;
    QMap<int, Device> m_Devices;
    QList<Device> m_ToAdd;
    QList<int> m_Forgotten;
    bool m_Found = false;
    bool m_Pairing = false;
    int m_JustPaired = -1;

    QLabel* m_pStatus;
    QPushButton* m_pButtonLookAgain;
    QPushButton* m_pButtonPair;
    QLabel* m_pPairingHelp;
    QVBoxLayout* m_pList;
    QLabel* m_pEmpty;
    QTimer* m_pNamedTimer;
};
