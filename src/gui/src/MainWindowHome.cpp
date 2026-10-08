/*
    GlideKVM -- mouse and keyboard sharing utility
    Copyright (C) GlideKVM contributors

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

// The main window's home screen: a sidebar, a role switch, and one page for
// each role. The widgets created by the .ui file and setupConnectionModeUi()
// keep their logic; this file only arranges them into the GlideKVM layout.

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "AppConfig.h"
#include "Theme.h"

#include <QAbstractSocket>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QButtonGroup>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QClipboard>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkInterface>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {

enum class NavIcon { Home, Arrange, Clipboard, Security, Settings, Log };

// Sidebar icons are drawn rather than loaded so they stay sharp at any scale.
QIcon nav_icon(NavIcon kind)
{
    auto paint = [kind](const QColor& color) {
        const qreal ratio = 2.0;
        QPixmap pixmap(QSize(24, 24) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        switch (kind) {
        case NavIcon::Home: {
            QPainterPath cursor;
            cursor.moveTo(5, 3);
            cursor.lineTo(19, 10);
            cursor.lineTo(13, 12);
            cursor.lineTo(11, 18);
            cursor.closeSubpath();
            painter.drawPath(cursor);
            break;
        }
        case NavIcon::Arrange:
            painter.drawRoundedRect(QRectF(2, 5, 8, 6), 1, 1);
            painter.drawRoundedRect(QRectF(14, 5, 8, 6), 1, 1);
            painter.drawRoundedRect(QRectF(8, 14, 8, 6), 1, 1);
            break;
        case NavIcon::Clipboard:
            painter.drawRoundedRect(QRectF(6, 4, 12, 17), 2, 2);
            painter.drawRect(QRectF(9, 3, 6, 3));
            painter.drawLine(QPointF(9, 11), QPointF(15, 11));
            painter.drawLine(QPointF(9, 15), QPointF(13, 15));
            break;
        case NavIcon::Security:
            painter.drawRoundedRect(QRectF(5, 11, 14, 10), 2, 2);
            painter.drawArc(QRectF(8, 4, 8, 14), 0, 180 * 16);
            break;
        case NavIcon::Settings:
            painter.drawLine(QPointF(4, 7), QPointF(14, 7));
            painter.drawLine(QPointF(18, 7), QPointF(20, 7));
            painter.drawLine(QPointF(4, 17), QPointF(8, 17));
            painter.drawLine(QPointF(12, 17), QPointF(20, 17));
            painter.drawEllipse(QPointF(16, 7), 2, 2);
            painter.drawEllipse(QPointF(10, 17), 2, 2);
            break;
        case NavIcon::Log:
            painter.drawRoundedRect(QRectF(5, 3, 14, 18), 2, 2);
            painter.drawLine(QPointF(9, 8), QPointF(15, 8));
            painter.drawLine(QPointF(9, 12), QPointF(15, 12));
            painter.drawLine(QPointF(9, 16), QPointF(13, 16));
            break;
        }
        return pixmap;
    };
    QIcon icon;
    icon.addPixmap(paint(QColor("#3A4A61")), QIcon::Normal, QIcon::Off);
    icon.addPixmap(paint(QColor(glidekvm::theme::kBlue)), QIcon::Normal, QIcon::On);
    return icon;
}

QFrame* make_card(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setProperty("card", true);
    return card;
}

QLabel* make_title(const QString& text, QWidget* parent, const char* role = "cardTitle")
{
    auto* title = new QLabel(text, parent);
    title->setProperty("role", role);
    title->setWordWrap(true);
    return title;
}

QLabel* make_muted(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "muted");
    label->setWordWrap(true);
    return label;
}

// The address people are most likely to type: a home or office network
// address, else the first one found.
QString preferred_address()
{
    QString first;
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto& address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol ||
            address == QHostAddress(QHostAddress::LocalHost)) {
            continue;
        }
        const QString text = address.toString();
        if (text.startsWith("192.168.")) {
            return text;
        }
        if (first.isEmpty() && !text.startsWith("169.254.")) {
            first = text;
        }
    }
    return first;
}

// Moves every row of a form into another form, keeping each widget's logic.
void move_form_rows(QFormLayout* from, QFormLayout* to)
{
    while (from->rowCount() > 0) {
        QFormLayout::TakeRowResult row = from->takeRow(0);
        QWidget* label = row.labelItem ? row.labelItem->widget() : nullptr;
        QWidget* field = row.fieldItem ? row.fieldItem->widget() : nullptr;
        QLayout* fieldLayout = row.fieldItem ? row.fieldItem->layout() : nullptr;
        if (label && field) {
            to->addRow(label, field);
        } else if (label && fieldLayout) {
            to->addRow(label, fieldLayout);
        } else if (field) {
            to->addRow(field);
        } else if (fieldLayout) {
            to->addRow(fieldLayout);
        } else if (label) {
            to->addRow(label);
        }
        // the layout items wrapping widgets are no longer used; a layout item
        // that is itself a layout now belongs to the new form
        if (row.labelItem && row.labelItem->widget()) {
            delete row.labelItem;
        }
        if (row.fieldItem && row.fieldItem->widget()) {
            delete row.fieldItem;
        }
    }
}

// Calls back when a watched widget is hidden, e.g. the log's own Hide button.
class HideWatcher : public QObject
{
public:
    HideWatcher(QObject* parent, std::function<void()> on_hide) :
        QObject(parent), on_hide_(std::move(on_hide)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Hide) {
            on_hide_();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> on_hide_;
};

} // namespace

void MainWindow::buildHomeLayout()
{
    // the original layout keeps the role group boxes, which hold whether this
    // computer is the server or a client; it stays alive but hidden
    QWidget* original = takeCentralWidget();
    original->setParent(this);
    original->hide();

    auto* root = new QWidget(this);
    root->setObjectName("homeRoot");
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- sidebar
    auto* sidebar = new QWidget(root);
    sidebar->setObjectName("sidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(208);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(14, 20, 14, 16);
    side->setSpacing(4);

    auto* brand = new QWidget(sidebar);
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(8, 0, 0, 16);
    brandLayout->setSpacing(10);
    auto* logo = new QLabel(brand);
    QIcon small_icon(QStringLiteral(":/res/icons/app/glidekvm-32.png"));
    small_icon.addFile(QStringLiteral(":/res/icons/app/glidekvm-48.png"));
    small_icon.addFile(QStringLiteral(":/res/icons/app/glidekvm-small-64.png"));
    small_icon.addFile(QStringLiteral(":/res/icons/app/glidekvm-small-96.png"));
    logo->setPixmap(small_icon.pixmap(30, 30));
    auto* wordmark = new QLabel(tr("GlideKVM"), brand);
    wordmark->setProperty("role", "wordmark");
    brandLayout->addWidget(logo);
    brandLayout->addWidget(wordmark);
    brandLayout->addStretch();
    side->addWidget(brand);

    auto makeNav = [sidebar, side](const QString& text, NavIcon icon) {
        auto* button = new QPushButton(nav_icon(icon), text, sidebar);
        button->setProperty("nav", true);
        button->setIconSize(QSize(20, 20));
        button->setCursor(Qt::PointingHandCursor);
        side->addWidget(button);
        return button;
    };
    m_pNavHome = makeNav(tr("Home"), NavIcon::Home);
    m_pNavArrange = makeNav(tr("Arrange screens"), NavIcon::Arrange);
    m_pNavClipboard = makeNav(tr("Clipboard && files"), NavIcon::Clipboard);
    m_pNavSecurity = makeNav(tr("Security"), NavIcon::Security);
    m_pNavSettings = makeNav(tr("Settings"), NavIcon::Settings);
    m_pNavLog = makeNav(tr("Activity log"), NavIcon::Log);
    // the sidebar shows which page is open
    auto* navGroup = new QButtonGroup(this);
    for (QPushButton* button : {m_pNavHome, m_pNavArrange, m_pNavClipboard, m_pNavSecurity,
                                m_pNavSettings, m_pNavLog}) {
        button->setCheckable(true);
        navGroup->addButton(button);
    }
    m_pNavHome->setChecked(true);
    side->addStretch();

    // what the organization manages, or a reassurance about encryption
    auto* note = new QFrame(sidebar);
    note->setProperty("tile", true);
    auto* noteLayout = new QVBoxLayout(note);
    noteLayout->setContentsMargins(12, 10, 12, 10);
    m_pSidebarNote = make_muted(QString(), note);
    noteLayout->addWidget(m_pSidebarNote);
    if (m_pLabelManaged) {
        m_pLabelManaged->setParent(note);
        m_pLabelManaged->setWordWrap(true);
        m_pLabelManaged->setProperty("role", "muted");
        noteLayout->addWidget(m_pLabelManaged);
    }
    if (m_pConnectionModeRow) {
        m_pConnectionModeRow->hide();
    }
    side->addWidget(note);

    connect(m_pNavHome, &QPushButton::clicked, this, [this]() { showHomePage(); });
    connect(m_pNavArrange, &QPushButton::clicked, this, [this]() {
        on_m_pButtonConfigureServer_clicked();
    });
    connect(m_pNavClipboard, &QPushButton::clicked, this, [this]() { showClipboardPage(); });
    connect(m_pNavSecurity, &QPushButton::clicked, this, [this]() { showSecurityPage(); });
    connect(m_pNavSettings, &QPushButton::clicked, ui_->m_pActionSettings, &QAction::trigger);
    connect(m_pNavLog, &QPushButton::clicked, ui_->m_pActionShowLog, &QAction::trigger);

    // ---- main column
    m_pHomeScroll = new QScrollArea(root);
    m_pHomeScroll->setWidgetResizable(true);
    m_pHomeScroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(m_pHomeScroll);
    content->setObjectName("homeContent");
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(28, 22, 28, 22);
    column->setSpacing(18);
    m_pHomeScroll->setWidget(content);

    // role switch and this computer's name
    auto* top = new QHBoxLayout();
    auto* roleSwitch = new QFrame(content);
    roleSwitch->setObjectName("roleSwitch");
    auto* roleLayout = new QHBoxLayout(roleSwitch);
    roleLayout->setContentsMargins(4, 4, 4, 4);
    roleLayout->setSpacing(4);
    m_pRoleServer = new QPushButton(tr("Share this keyboard && mouse"), roleSwitch);
    m_pRoleClient = new QPushButton(tr("Use another computer's"), roleSwitch);
    auto* roleGroup = new QButtonGroup(this);
    for (QPushButton* button : {m_pRoleServer, m_pRoleClient}) {
        button->setCheckable(true);
        button->setProperty("segment", true);
        button->setCursor(Qt::PointingHandCursor);
        roleGroup->addButton(button);
        roleLayout->addWidget(button);
        // the selected side is bold, so leave room for the bold text
        QFont bold = button->font();
        bold.setWeight(QFont::DemiBold);
        button->setMinimumWidth(QFontMetrics(bold).horizontalAdvance(button->text()) + 40);
    }
    m_pLabelThisComputer = make_muted(QString(), content);
    m_pLabelThisComputer->setWordWrap(false);
    top->addWidget(roleSwitch);
    top->addStretch();
    top->addWidget(m_pLabelThisComputer);
    column->addLayout(top);

    connect(m_pRoleServer, &QPushButton::clicked, this, [this]() {
        ui_->m_pGroupServer->setChecked(true);
    });
    connect(m_pRoleClient, &QPushButton::clicked, this, [this]() {
        ui_->m_pGroupClient->setChecked(true);
    });
    connect(ui_->m_pGroupServer, &QGroupBox::toggled, this, [this]() { updateHome(); });
    connect(ui_->m_pGroupClient, &QGroupBox::toggled, this, [this]() { updateHome(); });

    m_pHomePages = new QStackedWidget(content);
    column->addWidget(m_pHomePages);

    // ---- server page
    auto* serverPage = new QWidget(m_pHomePages);
    auto* serverColumn = new QVBoxLayout(serverPage);
    serverColumn->setContentsMargins(0, 0, 0, 0);
    serverColumn->setSpacing(18);

    m_pHero = new QFrame(serverPage);
    m_pHero->setObjectName("hero");
    auto* heroLayout = new QHBoxLayout(m_pHero);
    heroLayout->setContentsMargins(26, 22, 26, 22);
    heroLayout->setSpacing(18);
    auto* heroIcon = new QLabel(m_pHero);
    heroIcon->setPixmap(small_icon.pixmap(52, 52));
    heroIcon->setFixedSize(52, 52);
    auto* heroText = new QVBoxLayout();
    heroText->setSpacing(4);
    m_pHeroTitle = make_title(QString(), m_pHero, "heroTitle");
    m_pHeroText = make_title(QString(), m_pHero, "heroText");
    heroText->addWidget(m_pHeroTitle);
    heroText->addWidget(m_pHeroText);
    m_pHeroButtonSlot = new QHBoxLayout();
    heroLayout->addWidget(heroIcon, 0, Qt::AlignTop);
    heroLayout->addLayout(heroText, 1);
    heroLayout->addLayout(m_pHeroButtonSlot);
    serverColumn->addWidget(m_pHero);

    auto* serverGrid = new QGridLayout();
    serverGrid->setSpacing(18);

    // ways to connect: the addresses other computers use to reach this one
    auto* ways = make_card(serverPage);
    auto* waysLayout = new QVBoxLayout(ways);
    waysLayout->setContentsMargins(22, 20, 22, 20);
    waysLayout->setSpacing(12);
    waysLayout->addWidget(make_title(tr("Ways to connect"), ways));

    m_pNetworkTile = new QFrame(ways);
    m_pNetworkTile->setProperty("tile", true);
    auto* networkLayout = new QGridLayout(m_pNetworkTile);
    networkLayout->setContentsMargins(14, 12, 14, 12);
    networkLayout->setHorizontalSpacing(12);
    ui_->label_2->setText(tr("Network address"));
    ui_->label_2->setProperty("role", "muted");
    m_pLabelNetworkAddress = new QLabel(m_pNetworkTile);
    m_pLabelNetworkAddress->setProperty("role", "address");
    m_pLabelNetworkAddress->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pButtonCopyNetworkAddress = new QToolButton(m_pNetworkTile);
    m_pButtonCopyNetworkAddress->setText(tr("Copy"));
    m_pButtonCopyNetworkAddress->setToolTip(tr("Copy this address to the clipboard"));
    ui_->m_pLabelIpAddresses->setProperty("role", "muted");
    ui_->m_pLabelIpAddresses->setWordWrap(true);
    networkLayout->addWidget(ui_->label_2, 0, 0);
    networkLayout->addWidget(m_pLabelNetworkAddress, 1, 0);
    networkLayout->addWidget(m_pButtonCopyNetworkAddress, 0, 1, 2, 1, Qt::AlignVCenter);
    networkLayout->addWidget(ui_->m_pLabelIpAddresses, 2, 0, 1, 2);
    networkLayout->setColumnStretch(0, 1);
    waysLayout->addWidget(m_pNetworkTile);

    m_pBluetoothTile = new QFrame(ways);
    m_pBluetoothTile->setProperty("tile", true);
    auto* bluetoothLayout = new QVBoxLayout(m_pBluetoothTile);
    bluetoothLayout->setContentsMargins(14, 12, 14, 12);
    bluetoothLayout->setSpacing(2);
    m_pLabelBluetoothAddressTitle->setText(tr("Bluetooth address"));
    m_pLabelBluetoothAddressTitle->setProperty("role", "muted");
    m_pLabelBluetoothAddress->setProperty("role", "address");
    bluetoothLayout->addWidget(m_pLabelBluetoothAddressTitle);
    bluetoothLayout->addWidget(m_pBluetoothAddressField);
    waysLayout->addWidget(m_pBluetoothTile);

    m_pLabelServerBluetoothHint->setText(
        tr("On a VPN? Use Bluetooth: it doesn't go through the network, so the VPN can't "
           "block it. Pair the computers in <a href=\"ms-settings:bluetooth\">Bluetooth "
           "settings</a> first."));
    m_pLabelServerBluetoothHint->setProperty("role", "muted");
    waysLayout->addWidget(m_pLabelServerBluetoothHint);
    waysLayout->addStretch();

    connect(m_pButtonCopyNetworkAddress, &QToolButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_pLabelNetworkAddress->text());
        m_pButtonCopyNetworkAddress->setText(tr("Copied"));
        QTimer::singleShot(1500, m_pButtonCopyNetworkAddress, [this]() {
            m_pButtonCopyNetworkAddress->setText(tr("Copy"));
        });
    });

    // connected computers, followed from the server's log
    auto* clients = make_card(serverPage);
    auto* clientsLayout = new QVBoxLayout(clients);
    clientsLayout->setContentsMargins(22, 20, 22, 20);
    clientsLayout->setSpacing(10);
    clientsLayout->addWidget(make_title(tr("Connected computers"), clients));
    m_pListConnected = new QListWidget(clients);
    m_pListConnected->setObjectName("clientList");
    m_pListConnected->setIconSize(QSize(22, 22));
    m_pListConnected->setSelectionMode(QAbstractItemView::NoSelection);
    m_pListConnected->setFocusPolicy(Qt::NoFocus);
    m_pListConnected->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pListConnected->setAccessibleName(tr("Connected computers"));
    m_pLabelNoneConnected = make_muted(QString(), clients);
    clientsLayout->addWidget(m_pListConnected);
    clientsLayout->addWidget(m_pLabelNoneConnected);
    clientsLayout->addStretch();
    m_pLinkArrange = new QLabel(QString("<a href=\"#arrange\">%1</a>").arg(tr("Change where they sit")),
                                clients);
    connect(m_pLinkArrange, &QLabel::linkActivated, this, [this]() {
        on_m_pButtonConfigureServer_clicked();
    });
    clientsLayout->addWidget(m_pLinkArrange);

    // what crosses between the computers
    auto* between = make_card(serverPage);
    auto* betweenLayout = new QVBoxLayout(between);
    betweenLayout->setContentsMargins(22, 20, 22, 20);
    betweenLayout->setSpacing(12);
    betweenLayout->addWidget(make_title(tr("Between computers"), between));
    addSharingOptions(betweenLayout, between, &m_pHomeShareClipboard, &m_pHomeShareFiles);

    serverGrid->addWidget(ways, 0, 0);
    serverGrid->addWidget(clients, 0, 1);
    serverGrid->addWidget(between, 1, 0, 1, 2);
    serverGrid->setColumnStretch(0, 1);
    serverGrid->setColumnStretch(1, 1);
    serverColumn->addLayout(serverGrid);

    // where the other screens sit, or a configuration file
    auto* arrangement = make_card(serverPage);
    auto* arrangementLayout = new QVBoxLayout(arrangement);
    arrangementLayout->setContentsMargins(22, 20, 22, 20);
    arrangementLayout->setSpacing(10);
    arrangementLayout->addWidget(make_title(tr("Screen arrangement"), arrangement));
    ui_->m_pRadioInternalConfig->setText(tr("Arrange them here"));
    auto* internalRow = new QHBoxLayout();
    internalRow->addWidget(ui_->m_pRadioInternalConfig);
    ui_->m_pButtonConfigureServer->setText(tr("&Arrange screens..."));
    internalRow->addWidget(ui_->m_pButtonConfigureServer);
    internalRow->addStretch();
    arrangementLayout->addLayout(internalRow);
    ui_->m_pRadioExternalConfig->setText(tr("Use a configuration file"));
    arrangementLayout->addWidget(ui_->m_pRadioExternalConfig);
    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(ui_->m_pLabelConfigurationFile);
    fileRow->addWidget(ui_->m_pLineEditConfigFile, 1);
    fileRow->addWidget(ui_->m_pButtonBrowseConfigFile);
    arrangementLayout->addLayout(fileRow);
    serverColumn->addWidget(arrangement);
    serverColumn->addStretch();
    m_pHomePages->addWidget(serverPage);

    // ---- client page
    auto* clientPage = new QWidget(m_pHomePages);
    auto* clientGrid = new QGridLayout(clientPage);
    clientGrid->setContentsMargins(0, 0, 0, 0);
    clientGrid->setSpacing(18);

    auto* connectCard = make_card(clientPage);
    auto* connectLayout = new QVBoxLayout(connectCard);
    connectLayout->setContentsMargins(24, 22, 24, 22);
    connectLayout->setSpacing(14);
    m_pClientTitle = make_title(tr("Connect to your main computer"), connectCard, "pageTitle");
    connectLayout->addWidget(m_pClientTitle);
    auto* clientForm = new QFormLayout();
    clientForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    clientForm->setVerticalSpacing(10);
    move_form_rows(ui_->formLayout_3, clientForm);
    // the name is shown at the top of the window instead
    ui_->label_5->hide();
    ui_->m_pLabelScreenName->hide();
    connectLayout->addLayout(clientForm);
    m_pClientButtonSlot = new QVBoxLayout();
    connectLayout->addLayout(m_pClientButtonSlot);

    auto* nextCard = make_card(clientPage);
    auto* nextLayout = new QVBoxLayout(nextCard);
    nextLayout->setContentsMargins(24, 22, 24, 22);
    nextLayout->setSpacing(14);
    nextLayout->addWidget(make_title(tr("What happens next"), nextCard));
    const QStringList steps = {
        tr("A secure link is made"),
        tr("Everything is encrypted with TLS, and Bluetooth links must be paired too."),
        tr("The first time, check the fingerprint"),
        tr("GlideKVM shows your main computer's fingerprint. If it matches the one shown "
           "there, you're connected to the right computer."),
        tr("Move the mouse across"),
        tr("Your main computer decides which edge leads here, under Arrange screens."),
    };
    for (int i = 0; i < steps.size(); i += 2) {
        auto* step = new QHBoxLayout();
        step->setSpacing(12);
        auto* number = new QLabel(QString::number(i / 2 + 1), nextCard);
        number->setProperty("role", "stepNumber");
        number->setAlignment(Qt::AlignCenter);
        number->setFixedSize(30, 30);
        auto* words = new QVBoxLayout();
        words->setSpacing(2);
        auto* heading = new QLabel(steps[i], nextCard);
        heading->setProperty("role", "strong");
        words->addWidget(heading);
        words->addWidget(make_muted(steps[i + 1], nextCard));
        step->addWidget(number, 0, Qt::AlignTop);
        step->addLayout(words, 1);
        nextLayout->addLayout(step);
    }
    m_pCheckReconnect = new QCheckBox(tr("Reconnect automatically when GlideKVM opens"), nextCard);
    m_pCheckReconnect->setProperty("switch", true);
    nextLayout->addSpacing(4);
    nextLayout->addWidget(m_pCheckReconnect);
    connect(m_pCheckReconnect, &QCheckBox::toggled, this, [this](bool on) {
        appConfig().setAutoStart(on);
        appConfig().saveSettings();
    });

    clientGrid->addWidget(connectCard, 0, 0, Qt::AlignTop);
    clientGrid->addWidget(nextCard, 0, 1, Qt::AlignTop);
    clientGrid->setColumnStretch(0, 3);
    clientGrid->setColumnStretch(1, 2);
    m_pHomePages->addWidget(clientPage);

    // ---- status and security, for both roles
    auto* status = make_card(content);
    auto* statusLayout = new QVBoxLayout(status);
    statusLayout->setContentsMargins(22, 14, 22, 14);
    statusLayout->setSpacing(8);
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(ui_->m_pLabelPadlock);
    statusRow->addWidget(ui_->m_pStatusLabel, 1);
    statusRow->addWidget(ui_->m_pButtonReload);
    statusLayout->addLayout(statusRow);
    // the fingerprint lives on the Security page now
    ui_->m_pLabelFingerprint->hide();
    ui_->m_pLabelLocalFingerprint->hide();
    ui_->toolbutton_show_fingerprint->hide();
    ui_->frame_fingerprint_details->hide();
    auto* securityLink = new QLabel(QString("<a href=\"#security\">%1</a>")
                                        .arg(tr("Fingerprint and trusted computers")), status);
    connect(securityLink, &QLabel::linkActivated, this, [this]() { showSecurityPage(); });
    statusLayout->addWidget(securityLink);
    column->addWidget(status);
    column->addStretch();

    // ---- pages opened from the sidebar take the place of Home
    m_pMainStack = new QStackedWidget(root);
    m_pMainStack->addWidget(m_pHomeScroll);

    auto* panelScroll = new QScrollArea(m_pMainStack);
    panelScroll->setWidgetResizable(true);
    panelScroll->setFrameShape(QFrame::NoFrame);
    auto* panel = new QWidget(panelScroll);
    panel->setObjectName("homeContent");
    auto* panelColumn = new QVBoxLayout(panel);
    panelColumn->setContentsMargins(28, 22, 28, 22);
    panelColumn->setSpacing(16);
    m_pPanelTitle = make_title(QString(), panel, "pageTitle");
    panelColumn->addWidget(m_pPanelTitle);
    auto* panelCard = make_card(panel);
    m_pPanelLayout = new QVBoxLayout(panelCard);
    m_pPanelLayout->setContentsMargins(18, 16, 18, 16);
    panelColumn->addWidget(panelCard, 1);
    panelScroll->setWidget(panel);
    m_pMainStack->addWidget(panelScroll);

    // the log's own Hide button leads back to Home
    m_pLogWindow->installEventFilter(new HideWatcher(this, [this]() {
        // only its own Hide, not the whole window going to the tray
        if (m_pPanelWidget == m_pLogWindow && m_pLogWindow->isHidden()) {
            m_pPanelWidget = nullptr;
            showHomePage();
        }
    }));

    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(m_pMainStack, 1);
    setCentralWidget(root);

    updateHome();
}

void MainWindow::updateHome()
{
    if (m_pHomePages == nullptr) {
        return;
    }

    const bool server = app_role() == AppRole::Server;
    m_pRoleServer->setChecked(server);
    m_pRoleClient->setChecked(!server);
    m_pRoleServer->setEnabled(ui_->m_pGroupServer->isEnabled());
    m_pRoleClient->setEnabled(ui_->m_pGroupClient->isEnabled());
    m_pHomePages->setCurrentIndex(server ? 0 : 1);
    // size the stack to the page shown, not the taller of the two
    for (int i = 0; i < m_pHomePages->count(); ++i) {
        const auto policy = i == m_pHomePages->currentIndex() ? QSizePolicy::Preferred
                                                              : QSizePolicy::Ignored;
        m_pHomePages->widget(i)->setSizePolicy(policy, policy);
    }
    m_pHomePages->adjustSize();
    m_pNavArrange->setVisible(server);
    m_pLabelThisComputer->setText(tr("This computer: %1").arg(getScreenName()));

    if (m_pLabelManaged && m_policy.any()) {
        m_pSidebarNote->hide();
        m_pLabelManaged->show();
    } else {
        m_pSidebarNote->setText(appConfig().getCryptoEnabled()
            ? tr("Connections are encrypted.")
            : tr("Encryption is off. Turn it on in Settings."));
        m_pSidebarNote->show();
        if (m_pLabelManaged) {
            m_pLabelManaged->hide();
        }
    }

    // the one Start/Stop button sits where each role's main action is
    QPushButton* start = ui_->m_pButtonToggleStart;
    if (server) {
        m_pHeroButtonSlot->addWidget(start);
    } else {
        m_pClientButtonSlot->addWidget(start);
    }
    start->setMinimumHeight(44);

    // server: the banner says what is happening in plain words
    const AppConnectionState state = connection_state();
    const bool running = state == AppConnectionState::CONNECTED ||
                         state == AppConnectionState::TRANSFERRING;
    const int connected = m_ConnectedClients.size();
    if (state == AppConnectionState::CONNECTING) {
        m_pHeroTitle->setText(tr("Starting..."));
        m_pHeroText->setText(tr("Getting ready for your other computers."));
    } else if (!running) {
        m_pHeroTitle->setText(tr("Not sharing yet"));
        m_pHeroText->setText(tr("Start sharing so your other computers can connect to this one."));
    } else if (connected == 0) {
        m_pHeroTitle->setText(tr("Waiting for your other computers"));
        m_pHeroText->setText(tr("Open GlideKVM on another computer and connect to this one."));
    } else {
        m_pHeroTitle->setText(connected == 1 ? tr("Sharing with 1 computer")
                                             : tr("Sharing with %1 computers").arg(connected));
        m_pHeroText->setText(tr("Move the mouse off the edge of your screen to cross over."));
    }

    const QString address = preferred_address();
    m_pLabelNetworkAddress->setText(address.isEmpty() ? tr("Not connected to a network") : address);
    m_pButtonCopyNetworkAddress->setVisible(!address.isEmpty());
    // the full list only helps when there is more than one address
    ui_->m_pLabelIpAddresses->setVisible(ui_->m_pLabelIpAddresses->text().contains(','));
    m_pNetworkTile->setVisible(m_policy.network_allowed());
    m_pBluetoothTile->setVisible(server_accepts_bluetooth());

    m_pListConnected->clear();
    for (const QString& name : m_ConnectedClients) {
        QStringList parts{tr("Connected")};
        if (appConfig().getCryptoEnabled()) {
            parts << tr("encrypted");
        }
        const QString side = clientSide(name);
        if (!side.isEmpty()) {
            parts << side;
        }
        const QString detail = parts.join(QStringLiteral("  \u00b7  "));
        auto* item = new QListWidgetItem(QIcon(":/res/icons/48x48/computer.png"),
                                         name + "\n" + detail);
        m_pListConnected->addItem(item);
    }
    const int rowHeight = m_pListConnected->sizeHintForRow(0) > 0
        ? m_pListConnected->sizeHintForRow(0) : 44;
    m_pListConnected->setFixedHeight(rowHeight * connected + 4 * connected);
    m_pListConnected->setVisible(connected > 0);
    m_pLabelNoneConnected->setText(running
        ? tr("No computers yet. Connect from GlideKVM on another computer.")
        : tr("Computers appear here once you start sharing."));
    m_pLabelNoneConnected->setVisible(connected == 0);
    m_pLinkArrange->setVisible(ui_->m_pRadioInternalConfig->isChecked());

    syncSharingOptions(m_pHomeShareClipboard, m_pHomeShareFiles);
    if (m_pCheckReconnect) {
        QSignalBlocker block(m_pCheckReconnect);
        m_pCheckReconnect->setChecked(appConfig().getAutoStart());
    }
}

void MainWindow::trackConnectedClients(const QString& line)
{
    static const QRegularExpression connected("client \"([^\"]+)\" has connected");
    static const QRegularExpression gone(
        "client \"([^\"]+)\" (?:has disconnected|is dead)|disconnecting client \"([^\"]+)\"");

    QRegularExpressionMatch match = connected.match(line);
    if (match.hasMatch()) {
        const QString name = match.captured(1);
        if (!m_ConnectedClients.contains(name)) {
            m_ConnectedClients << name;
        }
        updateHome();
        return;
    }
    match = gone.match(line);
    if (match.hasMatch()) {
        const QString name = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
        m_ConnectedClients.removeAll(name);
        updateHome();
    }
}

void MainWindow::showPanel(const QString& title, QWidget* page, QPushButton* nav)
{
    if (m_pMainStack == nullptr) {
        // before the home screen exists, fall back to a separate window
        page->show();
        return;
    }

    if (m_pPanelWidget != page) {
        leavePanel();
        page->setParent(m_pPanelLayout->parentWidget());
        page->setWindowFlags(Qt::Widget);
        page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        page->setMinimumSize(0, 0);
        page->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        m_pPanelLayout->addWidget(page, 1);
        // as a page, the buttons save or go back rather than close a window
        if (auto* buttons = page->findChild<QDialogButtonBox*>()) {
            if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok)) {
                ok->setText(tr("&Save"));
                glidekvm::theme::set_primary(ok);
            }
        }
        if (auto* hide = page->findChild<QPushButton*>("m_pButtonHide")) {
            hide->setText(tr("&Back to Home"));
        }
        m_pPanelWidget = page;
    }
    m_pPanelTitle->setText(title);
    page->show();
    nav->setChecked(true);
    m_pMainStack->setCurrentIndex(1);

    // a page can be opened from the tray while the window is hidden
    if (!isVisible() || isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
}

void MainWindow::closePanel(QWidget* page)
{
    if (m_pPanelWidget == page) {
        m_pPanelWidget = nullptr;
        showHomePage();
    }
    if (page != m_pLogWindow) {
        page->deleteLater();
    }
}

void MainWindow::leavePanel()
{
    QWidget* page = m_pPanelWidget;
    if (page == nullptr) {
        return;
    }
    m_pPanelWidget = nullptr;
    m_pPanelLayout->removeWidget(page);
    if (page == m_pLogWindow) {
        page->hide();
        return;
    }
    // a page left without saving discards its changes, like Cancel
    page->disconnect(this);
    if (auto* dialog = qobject_cast<QDialog*>(page)) {
        dialog->reject();
    }
    page->deleteLater();
}

void MainWindow::showHomePage()
{
    leavePanel();
    if (m_pMainStack == nullptr) {
        return;
    }
    m_pNavHome->setChecked(true);
    m_pMainStack->setCurrentIndex(0);
    m_pHomeScroll->ensureVisible(0, 0);
}
