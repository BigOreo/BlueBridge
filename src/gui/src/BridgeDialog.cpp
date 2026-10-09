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

#include "BridgeDialog.h"

#include "bridge/BridgeBoard.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>

namespace {

QLabel* muted(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "muted");
    label->setWordWrap(true);
    return label;
}

} // namespace

BridgeConnection::BridgeConnection(QObject* parent) : QObject(parent)
{
}

BridgeConnection::~BridgeConnection()
{
    stop();
}

void BridgeConnection::start()
{
    stop();
    m_Stopping = false;
    m_Thread = std::thread([this]() { run(); });
}

void BridgeConnection::stop()
{
    m_Stopping = true;
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Port.reset();
}

bool BridgeConnection::send(const QString& line)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Port && m_Port->write(line.toStdString() + "\n");
}

void BridgeConnection::run()
{
    using clock = std::chrono::steady_clock;
    const auto until = clock::now() + std::chrono::seconds(6);
    auto port = std::make_unique<glidekvm::SerialPort>();
    glidekvm::SerialPort* probing = port.get();
    glidekvm::BridgeEvent hello;
    std::string path;
    bool ok = false;
    while (!m_Stopping && !ok && clock::now() < until) {
        ok = glidekvm::find_bridge_board(
            *port, "auto", m_Stopping, &hello, &path,
            [probing](const std::string& line) { return probing->write(line + "\n"); });
        if (!ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    if (!ok) {
        if (!m_Stopping) {
            QMetaObject::invokeMethod(this, [this]() { emit notFound(); }, Qt::QueuedConnection);
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Port = std::move(port);
    }
    const QString foundPath = QString::fromStdString(path);
    const QString version = QString::fromStdString(hello.version);
    QMetaObject::invokeMethod(this, [this, foundPath, version]() { emit found(foundPath, version); },
                              Qt::QueuedConnection);

    glidekvm::LineSplitter lines;
    char data[256];
    while (!m_Stopping) {
        const int n = probing->read(data, sizeof data, 100);
        if (n < 0) {
            QMetaObject::invokeMethod(this, [this]() { emit lost(); }, Qt::QueuedConnection);
            return;
        }
        lines.add(data, static_cast<std::size_t>(n));
        std::string line;
        while (lines.next(line)) {
            if (!line.empty() && line[0] == '@') {
                const QString text = QString::fromStdString(line);
                QMetaObject::invokeMethod(this, [this, text]() { emit lineReceived(text); },
                                          Qt::QueuedConnection);
            }
        }
    }
}

BridgeDialog::BridgeDialog(QWidget* parent, const QMap<int, QString>& onDesk) :
    QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint),
    m_pConnection(new BridgeConnection(this)),
    m_OnDesk(onDesk)
{
    setWindowTitle(tr("Phones and tablets"));
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Phones and tablets over Bluetooth"), this);
    title->setProperty("role", "cardTitle");
    layout->addWidget(title);
    layout->addWidget(muted(tr("Use this computer's keyboard and mouse on an iPad, an iPhone or "
                               "another phone or tablet, through the GlideKVM Bridge: a small "
                               "board plugged into a USB port here. The devices need nothing "
                               "installed: to them it is a Bluetooth keyboard and mouse."), this));

    auto* statusRow = new QHBoxLayout();
    m_pStatus = new QLabel(this);
    m_pStatus->setWordWrap(true);
    m_pStatus->setTextFormat(Qt::RichText);
    m_pStatus->setOpenExternalLinks(true);
    statusRow->addWidget(m_pStatus, 1);
    m_pButtonLookAgain = new QPushButton(tr("Look again"), this);
    connect(m_pButtonLookAgain, &QPushButton::clicked, this, [this]() { lookForBoard(); });
    statusRow->addWidget(m_pButtonLookAgain, 0, Qt::AlignTop);
    layout->addLayout(statusRow);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #E3E8EF;");
    layout->addWidget(line);

    auto* listTitle = new QLabel(tr("Paired with the bridge"), this);
    listTitle->setProperty("role", "strong");
    layout->addWidget(listTitle);
    m_pList = new QVBoxLayout();
    m_pList->setSpacing(6);
    layout->addLayout(m_pList);
    m_pEmpty = muted(QString(), this);
    layout->addWidget(m_pEmpty);

    m_pButtonPair = new QPushButton(tr("Pair a phone or tablet"), this);
    m_pButtonPair->setProperty("primary", true);
    connect(m_pButtonPair, &QPushButton::clicked, this, [this]() {
        if (m_Pairing) {
            stopPairing();
        } else {
            startPairing();
        }
    });
    layout->addWidget(m_pButtonPair, 0, Qt::AlignLeft);
    m_pPairingHelp = muted(tr("On the phone or tablet, open <b>Settings</b>, then <b>Bluetooth</b>, "
                              "and tap <b>GlideKVM Bridge</b>. If it asks, allow it to pair. "
                              "Waiting for up to two minutes..."), this);
    m_pPairingHelp->setTextFormat(Qt::RichText);
    m_pPairingHelp->hide();
    layout->addWidget(m_pPairingHelp);
    layout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);

    // the name comes just after pairing; don't wait for ever if it doesn't
    m_pNamedTimer = new QTimer(this);
    m_pNamedTimer->setSingleShot(true);
    m_pNamedTimer->setInterval(3000);
    connect(m_pNamedTimer, &QTimer::timeout, this, [this]() {
        if (m_JustPaired >= 0 && m_Devices.contains(m_JustPaired)) {
            m_ToAdd << m_Devices[m_JustPaired];
            m_JustPaired = -1;
            refreshList();
        }
    });

    connect(m_pConnection, &BridgeConnection::found, this, [this](const QString& port, const QString& version) {
        m_Found = true;
        showStatus(tr("<b>GlideKVM Bridge found</b> on %1, firmware %2.").arg(port, version));
        m_pConnection->send("list");
        refreshList();
    });
    connect(m_pConnection, &BridgeConnection::notFound, this, [this]() {
        m_Found = false;
        showStatus(tr("<b>Can't find the GlideKVM Bridge.</b> Plug it into a USB port, then click "
                      "Look again. A new board needs its software first: see "
                      "<a href=\"https://github.com/BigOreo/GlideKVM/tree/master/bridge\">how to "
                      "set up the bridge</a>."));
        refreshList();
    });
    connect(m_pConnection, &BridgeConnection::lost, this, [this]() {
        m_Found = false;
        m_Pairing = false;
        showStatus(tr("<b>The GlideKVM Bridge was unplugged.</b> Plug it back in, then click Look again."));
        refreshList();
    });
    connect(m_pConnection, &BridgeConnection::lineReceived, this, [this](const QString& line) {
        handleLine(line);
    });

    lookForBoard();
}

void BridgeDialog::showStatus(const QString& text)
{
    m_pStatus->setText(text);
}

void BridgeDialog::lookForBoard()
{
    m_Found = false;
    m_Devices.clear();
    showStatus(tr("Looking for the GlideKVM Bridge..."));
    refreshList();
    m_pConnection->start();
}

void BridgeDialog::handleLine(const QString& text)
{
    const glidekvm::BridgeEvent event = glidekvm::parse_bridge_event(text.toStdString());
    switch (event.type) {
    case glidekvm::BridgeEvent::Slot: {
        Device device;
        device.slot = event.slot;
        device.address = QString::fromStdString(event.address);
        device.name = QString::fromStdString(event.text);
        device.connected = event.state == "connected";
        m_Devices[event.slot] = device;
        break;
    }
    case glidekvm::BridgeEvent::Connected:
    case glidekvm::BridgeEvent::Disconnected:
        if (m_Devices.contains(event.slot)) {
            m_Devices[event.slot].connected = event.type == glidekvm::BridgeEvent::Connected;
        }
        break;
    case glidekvm::BridgeEvent::Paired: {
        Device device;
        device.slot = event.slot;
        device.address = QString::fromStdString(event.address);
        device.connected = true;
        m_Devices[event.slot] = device;
        m_Pairing = false;
        m_JustPaired = event.slot;
        m_pNamedTimer->start();
        break;
    }
    case glidekvm::BridgeEvent::Named:
        if (m_Devices.contains(event.slot)) {
            m_Devices[event.slot].name = QString::fromStdString(event.text);
            if (event.slot == m_JustPaired) {
                m_pNamedTimer->stop();
                m_ToAdd << m_Devices[event.slot];
                m_JustPaired = -1;
            }
        }
        break;
    case glidekvm::BridgeEvent::PairingEnded:
        if (m_Pairing) {
            m_Pairing = false;
            QMessageBox::information(this, tr("Pair a phone or tablet"),
                                     tr("Nothing paired in two minutes. Click Pair a phone or tablet "
                                        "to try again."));
        }
        break;
    case glidekvm::BridgeEvent::Full:
        m_Pairing = false;
        QMessageBox::information(this, tr("Pair a phone or tablet"),
                                 tr("The bridge already has eight devices. Forget one to make room."));
        break;
    case glidekvm::BridgeEvent::Forgot:
        m_Devices.remove(event.slot);
        if (!m_Forgotten.contains(event.slot)) {
            m_Forgotten << event.slot;
        }
        break;
    default:
        return;
    }
    refreshList();
}

void BridgeDialog::startPairing()
{
    if (m_pConnection->send("pair")) {
        m_Pairing = true;
        refreshList();
    }
}

void BridgeDialog::stopPairing()
{
    m_pConnection->send("pair stop");
    m_Pairing = false;
    refreshList();
}

void BridgeDialog::forget(int slot)
{
    const QString name = m_OnDesk.value(slot, m_Devices.value(slot).name);
    const auto answer = QMessageBox::question(
        this, tr("Forget %1").arg(name),
        tr("Forget %1? It comes off the desk, and has to pair again to be used. On the device, "
           "also forget GlideKVM Bridge in its Bluetooth settings.").arg(name));
    if (answer == QMessageBox::Yes) {
        m_pConnection->send(QString("forget %1").arg(slot));
    }
}

void BridgeDialog::refreshList()
{
    while (QLayoutItem* item = m_pList->takeAt(0)) {
        if (item->layout()) {
            while (QLayoutItem* inner = item->layout()->takeAt(0)) {
                delete inner->widget();
                delete inner;
            }
        }
        delete item->widget();
        delete item;
    }

    QList<int> added;
    for (const Device& device : m_ToAdd) {
        added << device.slot;
    }
    for (const Device& device : m_Devices) {
        auto* row = new QHBoxLayout();
        const QString name = m_OnDesk.value(device.slot, device.name.isEmpty() ? tr("New device")
                                                                                : device.name);
        QString state = device.connected ? tr("Connected") : tr("Not connected");
        if (added.contains(device.slot)) {
            state = tr("Paired, and goes on the desk when you close this");
        } else if (!m_OnDesk.contains(device.slot)) {
            state += tr(", not on the desk");
        }
        auto* words = new QWidget(this);
        auto* wordsLayout = new QVBoxLayout(words);
        wordsLayout->setContentsMargins(0, 0, 0, 0);
        wordsLayout->setSpacing(2);
        auto* nameLabel = new QLabel(name, words);
        nameLabel->setProperty("role", "strong");
        wordsLayout->addWidget(nameLabel);
        wordsLayout->addWidget(muted(state, words));
        row->addWidget(words, 1);
        if (!m_OnDesk.contains(device.slot) && !added.contains(device.slot)) {
            auto* add = new QPushButton(tr("Put on desk"), this);
            const Device copy = device;
            connect(add, &QPushButton::clicked, this, [this, copy]() {
                m_ToAdd << copy;
                refreshList();
            });
            row->addWidget(add);
        }
        auto* forgetButton = new QPushButton(tr("Forget"), this);
        const int slot = device.slot;
        connect(forgetButton, &QPushButton::clicked, this, [this, slot]() { forget(slot); });
        row->addWidget(forgetButton);
        m_pList->addLayout(row);
    }

    m_pEmpty->setVisible(m_Devices.isEmpty());
    m_pEmpty->setText(m_Found ? tr("Nothing paired yet.") : tr("Paired devices show here once the bridge is found."));
    m_pButtonLookAgain->setVisible(!m_Found);
    m_pButtonPair->setEnabled(m_Found);
    m_pButtonPair->setText(m_Pairing ? tr("Stop pairing") : tr("Pair a phone or tablet"));
    m_pPairingHelp->setVisible(m_Pairing);

    // rows come and go: grow rather than squeeze them, once they are laid out
    QTimer::singleShot(0, this, [this]() {
        layout()->activate();
        const int wanted = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width())
                                                         : sizeHint().height();
        if (wanted > height()) {
            resize(width(), wanted);
        }
    });
}
