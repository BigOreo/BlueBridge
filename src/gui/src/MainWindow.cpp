/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2008 Volker Lanz (vl@fidra.de)
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "AboutDialog.h"
#include "BluetoothAddress.h"
#include "ServerConfigDialog.h"
#include "SettingsDialog.h"
#include "ZeroconfService.h"
#include "FingerprintAcceptDialog.h"
#include "QUtility.h"
#include "SslCertificate.h"
#include "base/String.h"
#include "common/DataDirectories.h"
#include "net/FingerprintDatabase.h"
#include "net/SecureUtils.h"

#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#include <QNetworkAccessManager>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QPointer>
#include <QThread>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#if defined(Q_OS_MAC)
#include <ApplicationServices/ApplicationServices.h>
#endif

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <signal.h>

namespace {

static const QString allFilesFilter(QObject::tr("All files (*.*)"));
#if defined(Q_OS_WIN)
static const char APP_CONFIG_NAME[] = "input-leap.sgc";
static const QString APP_CONFIG_FILTER(QObject::tr("InputLeap Configurations (*.sgc)"));
// Bonjour used to be downloaded from binaries.symless.com, which no longer
// serves it. Point users at Apple's official installer instead.
static const char bonjourDownloadUrl[] = "https://support.apple.com/kb/DL999";
#else
static const char APP_CONFIG_NAME[] = "input-leap.conf";
static const QString APP_CONFIG_FILTER(QObject::tr("InputLeap Configurations (*.conf)"));
#endif
static const QString APP_CONFIG_OPEN_FILTER(APP_CONFIG_FILTER + ";;" + allFilesFilter);
static const QString APP_CONFIG_SAVE_FILTER(APP_CONFIG_FILTER);

const char* icon_file_for_connection_state(AppConnectionState state)
{
#if defined(Q_OS_MAC)
    switch (state) {
        default:
        case AppConnectionState::DISCONNECTED: return ":/res/icons/128x128/input-leap-disconnected-mask.png";
        case AppConnectionState::CONNECTING:   return ":/res/icons/128x128/input-leap-disconnected-mask.png";
        case AppConnectionState::CONNECTED:    return ":/res/icons/128x128/input-leap-connected-mask.png";
        case AppConnectionState::TRANSFERRING: return ":/res/icons/128x128/input-leap-transfering-mask.png";
    }
#else
    switch (state) {
        default:
        case AppConnectionState::DISCONNECTED: return ":/res/icons/128x128/input-leap-disconnected.png";
        case AppConnectionState::CONNECTING:   return ":/res/icons/128x128/input-leap-disconnected.png";
        case AppConnectionState::CONNECTED:    return ":/res/icons/128x128/input-leap-connected.png";
        case AppConnectionState::TRANSFERRING: return ":/res/icons/128x128/input-leap-transfering.png";
    }
#endif
}

const char* icon_name_for_connection_state(AppConnectionState state)
{
    switch (state) {
        default:
        case AppConnectionState::DISCONNECTED: return "input-leap-disconnected";
        case AppConnectionState::CONNECTING: return "input-leap-disconnected";
        case AppConnectionState::CONNECTED: return "input-leap-connected";
        case AppConnectionState::TRANSFERRING: return "input-leap-transfering";
    }
}

static const char* APP_LARGE_ICON = ":/res/icons/256x256/input-leap.png";

// Draws the icons for the connection mode toggle. They are painted rather
// than loaded so they stay sharp at any scale and follow the palette.
QPixmap paint_mode_icon(bool bluetooth, const QColor& color)
{
    const qreal ratio = 2.0;
    QPixmap pixmap(QSize(32, 32) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (bluetooth) {
        QPainterPath rune;
        rune.moveTo(9, 10);
        rune.lineTo(22, 22);
        rune.lineTo(16, 28);
        rune.lineTo(16, 4);
        rune.lineTo(22, 10);
        rune.lineTo(9, 22);
        painter.drawPath(rune);
    } else {
        painter.drawEllipse(QRectF(4, 4, 24, 24));
        painter.drawEllipse(QRectF(10.5, 4, 11, 24));
        painter.drawLine(QPointF(4, 16), QPointF(28, 16));
        painter.drawLine(QPointF(6.5, 10), QPointF(25.5, 10));
        painter.drawLine(QPointF(6.5, 22), QPointF(25.5, 22));
    }
    return pixmap;
}

QIcon mode_icon(bool bluetooth, const QPalette& palette)
{
    QIcon icon;
    icon.addPixmap(paint_mode_icon(bluetooth, palette.color(QPalette::ButtonText)),
                   QIcon::Normal, QIcon::Off);
    icon.addPixmap(paint_mode_icon(bluetooth, palette.color(QPalette::HighlightedText)),
                   QIcon::Normal, QIcon::On);
    return icon;
}

} // namespace

MainWindow::MainWindow(QSettings& settings, AppConfig& appConfig) :
    ui_{std::make_unique<Ui::MainWindow>()},
    m_Settings(settings),
    m_AppConfig(&appConfig),
    cmd_app_process_(nullptr),
    m_ServerConfig(&m_Settings, 5, 3, m_AppConfig->screenName(), this),
    m_pTempConfigFile(nullptr),
    m_pTrayIcon(nullptr),
    m_pTrayIconMenu(nullptr),
    m_AlreadyHidden(false),
    m_pMenuBar(nullptr),
    main_menu_(nullptr),
    m_pMenuHelp(nullptr),
    m_pZeroconfService(nullptr),
    m_SuppressAutoConfigWarning(false),
    m_SuppressEmptyServerWarning(false),
    m_ExpectedRunningState(kStopped),
    m_pSslCertificate(nullptr),
    m_pLogWindow(new LogWindow(nullptr))
{
    // explicitly unset DeleteOnClose so the window can be show and hidden
    // repeatedly until InputLeap is finished
    setAttribute(Qt::WA_DeleteOnClose, false);
    // mark the windows as sort of "dialog" window so that tiling window
    // managers will float it by default (X11)
    setAttribute(Qt::WA_X11NetWmWindowTypeDialog, true);

    ui_->setupUi(this);
    setWindowIcon(QIcon(APP_LARGE_ICON));
    createMenuBar();
    m_policy = inputleap::read_machine_policy();
    if (m_policy.encryption_required() && !m_AppConfig->getCryptoEnabled()) {
        m_AppConfig->setCryptoEnabled(true);
    }
    setupConnectionModeUi();
    loadSettings();
    initConnections();

    ui_->m_pLabelScreenName->setText(getScreenName());
    ui_->m_pLabelIpAddresses->setText(getIPAddresses());

#if defined(Q_OS_WIN)
    // ipc must always be enabled, so that we can disable command when switching to desktop mode.
    connect(&m_IpcClient, &IpcClient::readLogLine, this, &MainWindow::appendLogRaw);
    connect(&m_IpcClient, &IpcClient::errorMessage, this, &MainWindow::appendLogError);
    connect(&m_IpcClient, &IpcClient::infoMessage, this, &MainWindow::appendLogInfo);
    m_IpcClient.connectToHost();
#endif

    // change default size based on os
#if defined(Q_OS_MAC)
    resize(720, 550);
    setMinimumSize(720, 0);
#elif defined(Q_OS_LINUX)
    resize(700, 530);
    setMinimumSize(700, 0);
#endif

    m_SuppressAutoConfigWarning = true;
    ui_->m_pCheckBoxAutoConfig->setChecked(appConfig.autoConfig());
    m_SuppressAutoConfigWarning = false;

    ui_->m_pComboServerList->hide();
    updateConnectionModeUi();
    ui_->m_pLabelPadlock->hide();
    ui_->m_pLabelPadlock->setPixmap(QPixmap(":/res/icons/64x64/padlock.png").scaledToHeight(fontMetrics().height() * 1.5, Qt::SmoothTransformation));
    ui_->frame_fingerprint_details->hide();

    updateSSLFingerprint();

    connect(ui_->toolbutton_show_fingerprint, &QToolButton::clicked, this, [this](bool checked)
    {
        (void) checked;

        m_fingerprint_expanded = !m_fingerprint_expanded;
        if (m_fingerprint_expanded) {
            ui_->frame_fingerprint_details->show();
            ui_->toolbutton_show_fingerprint->setArrowType(Qt::ArrowType::UpArrow);
        } else {
            ui_->frame_fingerprint_details->hide();
            ui_->toolbutton_show_fingerprint->setArrowType(Qt::ArrowType::DownArrow);
        }
    });

    // resize window to smallest reasonable size
    resize(0, 0);
}

MainWindow::~MainWindow()
{
    if (appConfig().processMode() == Desktop) {
        m_ExpectedRunningState = kStopped;
        stopDesktop();
    }

    saveSettings();

    delete m_pZeroconfService;
    delete m_pSslCertificate;

    // LogWindow is created as a sibling of the MainWindow rather than a child
    // so that the main window can be hidden without hiding the log. because of
    // this it does not get properly cleaned up by the QObject system. also by
    // the time this destructor is called the event loop will no longer be able
    // to clean up the LogWindow so ->deleteLater() will not work
    delete m_pLogWindow;
}

void MainWindow::open()
{
    createTrayIcon();

    if (appConfig().getAutoHide()) {
        hide();
    } else {
        showNormal();
    }

    if (!appConfig().autoConfigPrompted()) {
        promptAutoConfig();
    }

    // only start if user has previously started. this stops the gui from
    // auto hiding before the user has configured InputLeap (which of course
    // confuses first time users, who think InputLeap has crashed).
    if (appConfig().startedBefore() && appConfig().getAutoStart()) {
        m_SuppressEmptyServerWarning = true;
        start_cmd_app();
        m_SuppressEmptyServerWarning = false;
    }
}

void MainWindow::setStatus(const QString &status)
{
    ui_->m_pStatusLabel->setText(status);
}

void MainWindow::createTrayIcon()
{
    m_pTrayIconMenu = new QMenu(this);

    m_pTrayIconMenu->addAction(ui_->m_pActionStartCmdApp);
    m_pTrayIconMenu->addAction(ui_->m_pActionStopCmdApp);
    m_pTrayIconMenu->addAction(ui_->m_pActionShowLog);
    m_pTrayIconMenu->addAction(ui_->m_pActionReload);
    m_pTrayIconMenu->addSeparator();

    m_pTrayIconMenu->addAction(ui_->m_pActionMinimize);
    m_pTrayIconMenu->addAction(ui_->m_pActionRestore);
    m_pTrayIconMenu->addSeparator();
    m_pTrayIconMenu->addAction(ui_->m_pActionQuit);

    m_pTrayIcon = new QSystemTrayIcon(this);
    m_pTrayIcon->setContextMenu(m_pTrayIconMenu);
    m_pTrayIcon->setToolTip("InputLeap");

    connect(m_pTrayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);

    set_icon(AppConnectionState::DISCONNECTED);

    m_pTrayIcon->show();
}

void MainWindow::retranslateMenuBar()
{
#ifndef Q_OS_DARWIN
    main_menu_->setTitle(tr("&InputLeap"));
    m_pMenuHelp->setTitle(tr("&Help"));
#else
    m_pMenuHelp->setTitle(tr("&File"));
    main_menu_->setTitle(tr("&Window"));
#endif

}

void MainWindow::createMenuBar()
{
    m_pMenuBar = new QMenuBar(this);
    main_menu_ = new QMenu("", m_pMenuBar);
    m_pMenuHelp = new QMenu("", m_pMenuBar);
    retranslateMenuBar();

#ifndef Q_OS_DARWIN
    m_pMenuBar->addAction(main_menu_->menuAction());
    m_pMenuBar->addAction(m_pMenuHelp->menuAction());
#else
    m_pMenuBar->addAction(m_pMenuHelp->menuAction());
    m_pMenuBar->addAction(main_menu_->menuAction());
#endif

    main_menu_->addAction(ui_->m_pActionShowLog);
    main_menu_->addAction(ui_->m_pActionSettings);
    main_menu_->addAction(ui_->m_pActionMinimize);
    main_menu_->addSeparator();

#ifndef Q_OS_DARWIN
    main_menu_->addAction(ui_->m_pActionSave);
#endif
    main_menu_->addSeparator();
    main_menu_->addAction(ui_->m_pActionQuit);
    m_pMenuHelp->addAction(ui_->m_pActionAbout);

#ifdef Q_OS_DARWIN
    m_pMenuHelp->addAction(ui_->m_pActionSave);
#endif

    setMenuBar(m_pMenuBar);
}

void MainWindow::loadSettings()
{
    // the next two must come BEFORE loading groupServerChecked and groupClientChecked or
    // disabling and/or enabling the right widgets won't automatically work
    ui_->m_pRadioExternalConfig->setChecked(settings().value("useExternalConfig", false).toBool());
    ui_->m_pRadioInternalConfig->setChecked(settings().value("useInternalConfig", true).toBool());

    ui_->m_pGroupServer->setChecked(settings().value("groupServerChecked", false).toBool());
    ui_->m_pLineEditConfigFile->setText(settings().value("configFile",
                                                    QDir::homePath() + "/" + APP_CONFIG_NAME).toString());
    ui_->m_pGroupClient->setChecked(settings().value("groupClientChecked", true).toBool());
    ui_->m_pLineEditHostname->setText(settings().value("serverHostname").toString());
    m_pLineEditServerBluetooth->setText(settings().value("serverBluetoothAddress").toString());

    auto mode = static_cast<ConnectionMode>(
                settings().value("connectionMode", static_cast<int>(ConnectionMode::Network)).toInt());
    if (!inputleap::is_bluetooth_supported() || !m_policy.bluetooth_allowed()) {
        mode = ConnectionMode::Network;
    } else if (!m_policy.network_allowed()) {
        mode = ConnectionMode::Bluetooth;
    }
    m_ConnectionMode = mode;
    m_pButtonModeBluetooth->setChecked(mode == ConnectionMode::Bluetooth);
    m_pButtonModeNetwork->setChecked(mode == ConnectionMode::Network);
}

void MainWindow::initConnections()
{
    connect(ui_->m_pActionMinimize, &QAction::triggered, this, &MainWindow::hide);
    connect(ui_->m_pComboServerList, &QComboBox::currentTextChanged, this, &MainWindow::comboServerList_currentIndexChanged);
    connect(ui_->m_pActionRestore, &QAction::triggered, this, &MainWindow::showNormal);
    connect(ui_->m_pActionStartCmdApp, &QAction::triggered, this, &MainWindow::start_cmd_app);
    connect(ui_->m_pActionStopCmdApp, &QAction::triggered, this, &MainWindow::stop_cmd_app);
    connect(ui_->m_pActionShowLog, &QAction::triggered, this, &MainWindow::showLogWindow);
    connect(ui_->m_pActionReload, &QAction::triggered, this, &MainWindow::restart_cmd_app);
    connect(ui_->m_pActionQuit, &QAction::triggered, qApp, &QCoreApplication::quit);
}

void MainWindow::saveSettings()
{
    // program settings
    settings().setValue("groupServerChecked", ui_->m_pGroupServer->isChecked());
    settings().setValue("useExternalConfig", ui_->m_pRadioExternalConfig->isChecked());
    settings().setValue("configFile", ui_->m_pLineEditConfigFile->text());
    settings().setValue("useInternalConfig", ui_->m_pRadioInternalConfig->isChecked());
    settings().setValue("groupClientChecked", ui_->m_pGroupClient->isChecked());
    settings().setValue("serverHostname", ui_->m_pLineEditHostname->text());
    settings().setValue("serverBluetoothAddress", m_pLineEditServerBluetooth->text());
    settings().setValue("connectionMode", static_cast<int>(m_ConnectionMode));

    settings().sync();
}

void MainWindow::set_icon(AppConnectionState state)
{
    if (m_pTrayIcon) {
        QIcon icon = QIcon::fromTheme(icon_name_for_connection_state(state),
                                      QIcon(icon_file_for_connection_state(state)));
#if defined(Q_OS_MAC)
        icon.setIsMask(true);
#endif
        m_pTrayIcon->setIcon(icon);
    }
}

void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick)
    {
        if (isVisible())
        {
            hide();
        }
        else
        {
            showNormal();
            activateWindow();
        }
    }
}

void MainWindow::logOutput()
{
    if (cmd_app_process_)
    {
        QString text(cmd_app_process_->readAllStandardOutput());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const auto results = text.split(QRegularExpression("\r|\n|\r\n"));
#else
        const auto results = text.split(QRegExp("\r|\n|\r\n"));
#endif
        for (const auto& line : results) {
            if (!line.isEmpty())
            {
                appendLogRaw(line);
            }
        }
    }
}

void MainWindow::logError()
{
    if (cmd_app_process_)
    {
        appendLogRaw(cmd_app_process_->readAllStandardError());
    }
}

void MainWindow::appendLogInfo(const QString& text)
{
    if (appConfig().logLevel() >= 3) {
        m_pLogWindow->appendInfo(text);
    }
}

void MainWindow::appendLogDebug(const QString& text) {
    if (appConfig().logLevel() >= 4) {
        m_pLogWindow->appendDebug(text);
    }
}

void MainWindow::appendLogError(const QString& text)
{
    m_pLogWindow->appendError(text);
}

void MainWindow::appendLogRaw(const QString& text)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto lines = text.split(QRegularExpression("\r|\n|\r\n"));
#else
    const auto lines = text.split(QRegExp("\r|\n|\r\n"));
#endif
    for (const auto& line : lines) {
        if (!line.isEmpty()) {
            m_pLogWindow->appendRaw(line);
            updateFromLogLine(line);
        }
    }
}

void MainWindow::updateFromLogLine(const QString &line)
{
    // TODO: this code makes Andrew cry
    checkConnected(line);
    checkFingerprint(line);
}

void MainWindow::checkConnected(const QString& line)
{
    // TODO: implement ipc connection state messages to replace this hack.
    if (line.contains("started server") ||
        line.contains("connected to server") ||
        line.contains("server status: active"))
    {
        set_connection_state(AppConnectionState::CONNECTED);

        if (!appConfig().startedBefore() && isVisible()) {
                QMessageBox::information(
                    this, "InputLeap",
                    tr("InputLeap is now connected. You can close the "
                    "config window and InputLeap will remain connected in "
                    "the background."));

            appConfig().setStartedBefore(true);
            appConfig().saveSettings();
        }
    }
}

void MainWindow::checkFingerprint(const QString& line)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QRegularExpression fingerprintRegex("peer fingerprint \\(SHA1\\): ([A-F0-9:]+) \\(SHA256\\): ([A-F0-9:]+)$");
    QRegularExpressionMatch match = fingerprintRegex.match(line);
    if (!match.hasMatch()) {
        return;
    }

    auto match1 = match.captured(1).toStdString();
    auto match2 = match.captured(2).toStdString();
#else
    QRegExp fingerprintRegex(".*peer fingerprint \\(SHA1\\): ([A-F0-9:]+) \\(SHA256\\): ([A-F0-9:]+)");
    if (!fingerprintRegex.exactMatch(line)) {
        return;
    }

    auto match1 = fingerprintRegex.cap(1).toStdString();
    auto match2 = fingerprintRegex.cap(2).toStdString();
#endif

    inputleap::FingerprintData fingerprint_sha1 = {
        inputleap::fingerprint_type_to_string(inputleap::FingerprintType::SHA1),
        inputleap::string::from_hex(match1)
    };

    inputleap::FingerprintData fingerprint_sha256 = {
        inputleap::fingerprint_type_to_string(inputleap::FingerprintType::SHA256),
        inputleap::string::from_hex(match2)
    };

    bool is_client = app_role() == AppRole::Client;

    auto db_path = is_client
            ? inputleap::DataDirectories::trusted_servers_ssl_fingerprints_path()
            : inputleap::DataDirectories::trusted_clients_ssl_fingerprints_path();

    auto db_dir = db_path.parent_path();
    if (!inputleap::fs::exists(db_dir)) {
        inputleap::fs::create_directories(db_dir);
    }

    // We compare only SHA256 fingerprints, but show both SHA1 and SHA256 so that the users can
    // still verify fingerprints on old InputLeap servers. This way the only time when we are
    // exposed to SHA1 vulnerabilities is when the user is reconnecting again.
    inputleap::FingerprintDatabase db;
    db.read(db_path);
    if (db.is_trusted(fingerprint_sha256)) {
        return;
    }

    static bool messageBoxAlreadyShown = false;

    if (!messageBoxAlreadyShown) {
        if (is_client) {
            stop_cmd_app();
        }

        messageBoxAlreadyShown = true;
        FingerprintAcceptDialog dialog{this, app_role(), fingerprint_sha1, fingerprint_sha256};
        if (dialog.exec() == QDialog::Accepted) {
            // restart core process after trusting fingerprint.
            db.add_trusted(fingerprint_sha256);
            db.write(db_path);
            if (is_client) {
                start_cmd_app();
            }
        }

        messageBoxAlreadyShown = false;
    }
}

void MainWindow::restart_cmd_app()
{
    stop_cmd_app();
    start_cmd_app();
}

void MainWindow::proofreadInfo()
{
    AppConnectionState old = connection_state_;
    connection_state_ = AppConnectionState::DISCONNECTED;
    set_connection_state(old);
}

void MainWindow::start_cmd_app()
{
    bool desktopMode = appConfig().processMode() == Desktop;
    bool serviceMode = appConfig().processMode() == Service;

    appendLogDebug("starting process");
    m_ExpectedRunningState = kStarted;
    set_connection_state(AppConnectionState::CONNECTING);

    QString app;
    QStringList args;

    args << "-f" << "--no-tray" << "--debug" << appConfig().logLevelText();


    args << "--name" << getScreenName();

    if (desktopMode)
    {
        cmd_app_process_ = new QProcess(this);
    }
    else
    {
        // tell client/server to talk to daemon through ipc.
        args << "--ipc";

#if defined(Q_OS_WIN)
        // tell the client/server to shut down when a ms windows desk
        // is switched; this is because we may need to elevate or not
        // based on which desk the user is in (login always needs
        // elevation, where as default desk does not).
        // Note that this is only enabled when InputLeap is set to elevate
        // 'as needed' (e.g. on a UAC dialog popup) in order to prevent
        // unnecessary restarts when InputLeap was started elevated or
        // when it is not allowed to elevate. In these cases restarting
        // the server is fruitless.
        if (appConfig().elevateMode() == ElevateAsNeeded) {
                args << "--stop-on-desk-switch";
        }
#endif
    }

#ifndef Q_OS_LINUX

    if (m_ServerConfig.enableDragAndDrop()) {
        args << "--enable-drag-drop";
    }

#endif

    if (!m_AppConfig->getCryptoEnabled()) {
        args << "--disable-crypto";
    }

#if defined(Q_OS_WIN)
    // on windows, the profile directory changes depending on the user that
    // launched the process (e.g. when launched with elevation). setting the
    // profile dir on launch ensures it uses the same profile dir is used
    // no matter how its relaunched.
    args << "--profile-dir" << QString::fromStdString("\"" + inputleap::DataDirectories::profile().u8string() + "\"");
#endif

    if ((app_role() == AppRole::Client && !clientArgs(args, app))
        || (app_role() == AppRole::Server && !serverArgs(args, app)))
    {
        stop_cmd_app();
        return;
    }

    if (desktopMode)
    {
        connect(cmd_app_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::cmd_app_finished);
        connect(cmd_app_process_, &QProcess::readyReadStandardOutput, this, &MainWindow::logOutput);
        connect(cmd_app_process_, &QProcess::readyReadStandardError, this, &MainWindow::logError);
    }

    m_pLogWindow->startNewInstance();

    appendLogInfo("starting " + QString(app_role() == AppRole::Server ? "server" : "client"));

    qDebug() << args;

    appendLogDebug(QString("command: %1 %2").arg(app, args.join(" ")));

    appendLogInfo("config file: " + configFilename());
    appendLogInfo("log level: " + appConfig().logLevelText());

    if (appConfig().logToFile())
        appendLogInfo("log file: " + appConfig().logFilename());

    if (desktopMode)
    {
        cmd_app_process_->start(app, args);
        if (!cmd_app_process_->waitForStarted())
        {
            show();
            QMessageBox::warning(this, tr("Program can not be started"), QString(tr("The executable<br><br>%1<br><br>could not be successfully started, although it does exist. Please check if you have sufficient permissions to run this program.").arg(app)));
            return;
        }
    }

    if (serviceMode)
    {
        QString command(app + " " + args.join(" "));
        m_IpcClient.sendCommand(command, appConfig().elevateMode());
    }
}

void MainWindow::setServerMode(bool isServerMode)
{
    ui_->m_pGroupServer->setChecked(isServerMode);
    ui_->m_pGroupClient->setChecked(!isServerMode);
}

bool MainWindow::clientArgs(QStringList& args, QString& app)
{
    app = appPath(appConfig().client_name());

    if (!QFile::exists(app))
    {
        show();
        QMessageBox::warning(this, tr("InputLeap client not found"),
                             tr("The executable for the InputLeap client does not exist."));
        return false;
    }

#if defined(Q_OS_WIN)
    // wrap in quotes so a malicious user can't start \Program.exe as admin.
    app = QString("\"%1\"").arg(app);
#endif

    if (appConfig().logToFile())
    {
        appConfig().persistLogDir();
        args << "--log" << appConfig().logFilenameCmd();
    }

    if (connection_mode() == ConnectionMode::Bluetooth) {
        QString serverAddress = inputleap::normalize_bluetooth_address(m_pLineEditServerBluetooth->text());
        if (serverAddress.isEmpty()) {
            show();
            if (!m_SuppressEmptyServerWarning) {
                QMessageBox::warning(this, tr("Server Bluetooth address missing"),
                    tr("Enter the server's Bluetooth address, for example 00:28:F8:8F:56:C3.\n\n"
                       "To find it, switch the server computer to Bluetooth mode: "
                       "its address is shown on its main window."));
            }
            return false;
        }
        args << "[bt:" + serverAddress + "]:" + QString::number(appConfig().port());
        return true;
    }

    // check auto config first, if it is disabled or no server detected,
    // use line edit host name if it is not empty
    if (ui_->m_pCheckBoxAutoConfig->isChecked()) {
        if (ui_->m_pComboServerList->count() != 0) {
            QString serverIp = ui_->m_pComboServerList->currentText();
            args << "[" + serverIp + "]:" + QString::number(appConfig().port());
            return true;
        }
    } else if (ui_->m_pLineEditHostname->text().isEmpty()) {
        show();
        if (!m_SuppressEmptyServerWarning) {
            QMessageBox::warning(this, tr("Hostname is empty"),
                             tr("Please fill in a hostname for the InputLeap client to connect to."));
        }
        return false;
    }

    args << "[" + ui_->m_pLineEditHostname->text() + "]:" + QString::number(appConfig().port());

    return true;
}

QString MainWindow::configFilename()
{
    QString filename;
    if (ui_->m_pRadioInternalConfig->isChecked())
    {
        // TODO: no need to use a temporary file, since we need it to
        // be permanent (since it'll be used for Windows services, etc).
        m_pTempConfigFile = new QTemporaryFile();
        if (!m_pTempConfigFile->open())
        {
            QMessageBox::critical(this, tr("Cannot write configuration file"),
                                  tr("The temporary configuration file required to start InputLeap can not be written."));
            return "";
        }

        serverConfig().save(*m_pTempConfigFile);
        filename = m_pTempConfigFile->fileName();

        m_pTempConfigFile->close();
    }
    else
    {
        if (!QFile::exists(ui_->m_pLineEditConfigFile->text()))
        {
            if (QMessageBox::warning(this, tr("Configuration filename invalid"),
                tr("You have not filled in a valid configuration file for the InputLeap server. "
                        "Do you want to browse for the configuration file now?"), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes
                    || !on_m_pButtonBrowseConfigFile_clicked())
                return "";
        }

        filename = ui_->m_pLineEditConfigFile->text();
    }
    return filename;
}

AppRole MainWindow::app_role() const
{
    return ui_->m_pGroupClient->isChecked() ? AppRole::Client : AppRole::Server;
}

QString MainWindow::hostname() const
{
    return ui_->m_pLineEditHostname->text();
}

QString MainWindow::address()
{
    // Bluetooth alone when the organization turned the network off
    if (!m_policy.network_allowed() && server_accepts_bluetooth()) {
        return "[bt]:" + QString::number(appConfig().port());
    }

    QString address = appConfig().networkInterface();
    if (!address.isEmpty())
        address = "[" + address + "]";
    return address + ":" + QString::number(appConfig().port());
}

QString MainWindow::appPath(const QString& name)
{
    return appConfig().program_dir() + name;
}

bool MainWindow::serverArgs(QStringList& args, QString& app)
{
    app = appPath(appConfig().server_name());

    if (!QFile::exists(app))
    {
        QMessageBox::warning(this, tr("InputLeap server not found"),
                             tr("The executable for the InputLeap server does not exist."));
        return false;
    }

#if defined(Q_OS_WIN)
    // wrap in quotes so a malicious user can't start \Program.exe as admin.
    app = QString("\"%1\"").arg(app);
#endif

    if (appConfig().logToFile())
    {
        appConfig().persistLogDir();

        args << "--log" << appConfig().logFilenameCmd();
    }

    if (!appConfig().getRequireClientCertificate()) {
        args << "--disable-client-cert-checking";
    }

    QString configFilename = this->configFilename();
#if defined(Q_OS_WIN)
    // wrap in quotes in case username contains spaces.
    configFilename = QString("\"%1\"").arg(configFilename);
#endif
    args << "-c" << configFilename << "--address" << address();
    if (m_policy.network_allowed() && server_accepts_bluetooth()) {
        args << "--bluetooth";
    }

    return true;
}

void MainWindow::stop_cmd_app()
{
    appendLogDebug("stopping process");

    m_ExpectedRunningState = kStopped;

    if (appConfig().processMode() == Service)
    {
        stopService();
    }
    else if (appConfig().processMode() == Desktop)
    {
        stopDesktop();
    }

    set_connection_state(AppConnectionState::DISCONNECTED);

    // HACK: deleting the object deletes the physical file, which is
    // bad, since it could be in use by the Windows service!
#if !defined(Q_OS_WIN)
    delete m_pTempConfigFile;
#endif
    m_pTempConfigFile = nullptr;

    // reset so that new connects cause auto-hide.
    m_AlreadyHidden = false;
}

void MainWindow::stopService()
{
    // send empty command to stop service from launching anything.
    m_IpcClient.sendCommand("", appConfig().elevateMode());
}

void MainWindow::stopDesktop()
{
    QMutexLocker locker(&m_StopDesktopMutex);
    if (!cmd_app_process_) {
        return;
    }

    appendLogInfo("stopping InputLeap desktop process");

    if (cmd_app_process_->isOpen()) {
#if SYSAPI_UNIX
        kill(cmd_app_process_->processId(), SIGTERM);
        cmd_app_process_->waitForFinished(5000);
#endif
        cmd_app_process_->close();
    }

    delete cmd_app_process_;
    cmd_app_process_ = nullptr;
}

void MainWindow::cmd_app_finished(int exitCode, QProcess::ExitStatus)
{
    if (exitCode == 0) {
        appendLogInfo(QString("process exited normally"));
    }
    else {
        appendLogError(QString("process exited with error code: %1").arg(exitCode));
    }

    if (m_ExpectedRunningState == kStarted) {
        QTimer::singleShot(1000, this, &MainWindow::start_cmd_app);
        appendLogInfo(QString("detected process not running, auto restarting"));
    }
    else {
        set_connection_state(AppConnectionState::DISCONNECTED);
    }
}

void MainWindow::set_connection_state(AppConnectionState state)
{
    if (connection_state() == state)
        return;

    if (state == AppConnectionState::CONNECTED || state == AppConnectionState::CONNECTING)
    {
        disconnect(ui_->m_pButtonToggleStart, &QPushButton::clicked, ui_->m_pActionStartCmdApp, &QAction::trigger);
        connect(ui_->m_pButtonToggleStart, &QPushButton::clicked, ui_->m_pActionStopCmdApp, &QAction::trigger);
        ui_->m_pButtonToggleStart->setText(tr("&Stop"));
        ui_->m_pButtonReload->setEnabled(true);
    }
    else if (state == AppConnectionState::DISCONNECTED)
    {
        disconnect(ui_->m_pButtonToggleStart, &QPushButton::clicked, ui_->m_pActionStopCmdApp, &QAction::trigger);
        connect(ui_->m_pButtonToggleStart, &QPushButton::clicked, ui_->m_pActionStartCmdApp, &QAction::trigger);
        ui_->m_pButtonToggleStart->setText(tr("&Start"));
        ui_->m_pButtonReload->setEnabled(false);
    }

    bool connected = false;
    if (state == AppConnectionState::CONNECTED || state == AppConnectionState::TRANSFERRING) {
        connected = true;
    }

    ui_->m_pActionStartCmdApp->setEnabled(!connected);
    ui_->m_pActionStopCmdApp->setEnabled(connected);

    switch (state)
    {
    case AppConnectionState::CONNECTED: {
        if (m_AppConfig->getCryptoEnabled()) {
            ui_->m_pLabelPadlock->show();
        }
        else {
            ui_->m_pLabelPadlock->hide();
        }

        setStatus(tr("InputLeap is running."));

        break;
    }
    case AppConnectionState::CONNECTING:
        ui_->m_pLabelPadlock->hide();
        setStatus(tr("InputLeap is starting."));
        break;
    case AppConnectionState::DISCONNECTED:
        ui_->m_pLabelPadlock->hide();
        setStatus(tr("InputLeap is not running."));
        break;
    case AppConnectionState::TRANSFERRING:
        break;
    default:
        break;
    }

    set_icon(state);

    connection_state_ = state;
}

void MainWindow::setVisible(bool visible)
{
    QMainWindow::setVisible(visible);
    ui_->m_pActionMinimize->setEnabled(visible);
    ui_->m_pActionRestore->setEnabled(!visible);

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070 // lion
    // dock hide only supported on lion :(
    ProcessSerialNumber psn = { 0, kCurrentProcess };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GetCurrentProcess(&psn);
#pragma GCC diagnostic pop
    if (visible)
        TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    else
        TransformProcessType(&psn, kProcessTransformToBackgroundApplication);
#endif
}

QString MainWindow::getIPAddresses()
{
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();

    bool hinted = false;
    QString result;
    for (int i = 0; i < addresses.size(); i++) {
        if (addresses[i].protocol() == QAbstractSocket::IPv4Protocol &&
            addresses[i] != QHostAddress(QHostAddress::LocalHost)) {

            QString address = addresses[i].toString();
            QString format = "%1, ";

            // usually 192.168.x.x is a useful ip for the user, so indicate
            // this by making it bold.
            if (!hinted && address.startsWith("192.168")) {
                hinted = true;
                format = "<b>%1</b>, ";
            }

            result += format.arg(address);
        }
    }

    if (result == "") {
        return tr("Unknown");
    }

    // remove trailing comma.
    result.chop(2);

    return result;
}

QString MainWindow::getScreenName()
{
    if (appConfig().screenName() == "") {
        return QHostInfo::localHostName();
    }
    else {
        return appConfig().screenName();
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event != nullptr)
    {
        switch (event->type())
        {
        case QEvent::LanguageChange:
        {
            ui_->retranslateUi(this);
            retranslateMenuBar();

            proofreadInfo();

            break;
        }
        case QEvent::WindowStateChange:
        {
            windowStateChanged();
            break;
        }
        default:
        {
            break;
        }
        }
    }
    // all that do not return are allowing the event to propagate
    QMainWindow::changeEvent(event);
}

bool MainWindow::event(QEvent* event)
{
    if (event->type() == QEvent::LayoutRequest) {
        setFixedSize(sizeHint());
    }
    return QMainWindow::event(event);
}

void MainWindow::updateZeroconfService()
{
    QMutexLocker locker(&m_UpdateZeroconfMutex);

    if (isBonjourRunning()) {
        if (!m_AppConfig->wizardShouldRun()) {
            if (m_pZeroconfService) {
                delete m_pZeroconfService;
                m_pZeroconfService = nullptr;
            }

            if (m_AppConfig->autoConfig() || app_role() == AppRole::Server) {
                m_pZeroconfService = new ZeroconfService(this);
            }
        }
    }
}

void MainWindow::serverDetected(const QString name)
{
    if (ui_->m_pComboServerList->findText(name) == -1) {
        // Note: the first added item triggers startInputLeap
        ui_->m_pComboServerList->addItem(name);
    }

    if (ui_->m_pComboServerList->count() > 1 && connection_mode() == ConnectionMode::Network) {
        ui_->m_pComboServerList->show();
    }
}

void MainWindow::updateSSLFingerprint()
{
    if (m_AppConfig->getCryptoEnabled() && m_pSslCertificate == nullptr) {
        m_pSslCertificate = new SslCertificate(this);
        connect(m_pSslCertificate, &SslCertificate::info, this, &MainWindow::appendLogInfo);
        m_pSslCertificate->generateCertificate();
    }

    ui_->toolbutton_show_fingerprint->setEnabled(false);
    ui_->m_pLabelLocalFingerprint->setText("Disabled");

    if (!m_AppConfig->getCryptoEnabled()) {
        return;
    }

    auto local_path = inputleap::DataDirectories::local_ssl_fingerprints_path();
    if (!inputleap::fs::exists(local_path)) {
        return;
    }

    inputleap::FingerprintDatabase db;
    db.read(local_path);
    if (db.fingerprints().size() != 2) {
        return;
    }

    for (const auto& fingerprint : db.fingerprints()) {
        if (fingerprint.algorithm == "sha1") {
            auto fingerprint_str = inputleap::format_ssl_fingerprint(fingerprint.data);
            ui_->label_sha1_fingerprint_full->setText(QString::fromStdString(fingerprint_str));
            continue;
        }

        if (fingerprint.algorithm == "sha256") {
            auto fingerprint_str = inputleap::format_ssl_fingerprint(fingerprint.data);
            fingerprint_str.resize(40);
            fingerprint_str += " ...";

            auto fingerprint_str_cols = inputleap::format_ssl_fingerprint_columns(fingerprint.data);
            auto fingerprint_randomart = inputleap::create_fingerprint_randomart(fingerprint.data);

            ui_->m_pLabelLocalFingerprint->setText(QString::fromStdString(fingerprint_str));
            ui_->label_sha256_fingerprint_full->setText(QString::fromStdString(fingerprint_str_cols));
            ui_->label_sha256_randomart->setText(QString::fromStdString(fingerprint_randomart));
        }
    }

    ui_->toolbutton_show_fingerprint->setEnabled(true);
}

void MainWindow::on_m_pGroupClient_toggled(bool on)
{
    ui_->m_pGroupServer->setChecked(!on);
    if (on) {
        updateZeroconfService();
    }
}

void MainWindow::on_m_pGroupServer_toggled(bool on)
{
    ui_->m_pGroupClient->setChecked(!on);
    if (on) {
        updateZeroconfService();
    }
}

bool MainWindow::on_m_pButtonBrowseConfigFile_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Browse for a InputLeap config file"), QString(), APP_CONFIG_OPEN_FILTER);

    if (!fileName.isEmpty())
    {
        ui_->m_pLineEditConfigFile->setText(fileName);
        return true;
    }

    return false;
}

bool MainWindow::on_m_pActionSave_triggered()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save configuration as..."), QString(), APP_CONFIG_SAVE_FILTER);

    if (!fileName.isEmpty() && !serverConfig().save(fileName))
    {
        QMessageBox::warning(this, tr("Save failed"), tr("Could not save configuration to file."));
        return true;
    }

    return false;
}

void MainWindow::on_m_pActionAbout_triggered()
{
    AboutDialog(this, appPath(appConfig().client_name())).exec();
}

void MainWindow::on_m_pActionSettings_triggered()
{
    auto dialog = std::make_unique<SettingsDialog>(this, appConfig());
    connect(dialog.get(), &SettingsDialog::requestLanguageChange, this, &MainWindow::requestLanguageChange);
    if (dialog.get()->exec() == QDialog::Accepted)
        updateSSLFingerprint();
    disconnect(dialog.get(), &SettingsDialog::requestLanguageChange, this, &MainWindow::requestLanguageChange);
}

void MainWindow::autoAddScreen(const QString name)
{
    if (!m_ServerConfig.ignoreAutoConfigClient()) {
        int r = m_ServerConfig.autoAddScreen(name);
        if (r != kAutoAddScreenOk) {
            switch (r) {
            case kAutoAddScreenManualServer:
                showConfigureServer(
                    tr("Please add the server (%1) to the grid.")
                        .arg(appConfig().screenName()));
                break;

            case kAutoAddScreenManualClient:
                showConfigureServer(
                    tr("Please drag the new client screen (%1) "
                        "to the desired position on the grid.")
                        .arg(name));
                break;
            default:
                break;
            }
        }
        else {
            restart_cmd_app();
        }
    }
}

void MainWindow::showConfigureServer(const QString& message)
{
    ServerConfigDialog dlg(this, serverConfig(), appConfig().screenName());
    dlg.message(message);
    dlg.exec();
}

void MainWindow::on_m_pButtonConfigureServer_clicked()
{
    showConfigureServer();
}

void MainWindow::on_m_pButtonReload_clicked()
{
    restart_cmd_app();
}

#if defined(Q_OS_WIN)
bool MainWindow::isServiceRunning(QString name)
{
    SC_HANDLE hSCManager;
    hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (hSCManager == nullptr) {
        appendLogError("failed to open a service controller manager, error: " +
            GetLastError());
        return false;
    }

    auto array = name.toLocal8Bit();

#if QT_VERSION_MAJOR < 6
    SC_HANDLE hService = OpenService(hSCManager, array.data(), SERVICE_QUERY_STATUS);
#else
    SC_HANDLE hService = OpenService(hSCManager, reinterpret_cast<LPCWSTR>(array.data()), SERVICE_QUERY_STATUS);
#endif
    if (hService == nullptr) {
        appendLogDebug("failed to open service: " + name);
        return false;
    }

    SERVICE_STATUS status;
    if (QueryServiceStatus(hService, &status)) {
        if (status.dwCurrentState == SERVICE_RUNNING) {
            return true;
        }
    }

    return false;
}
#else
bool MainWindow::isServiceRunning()
{
    return false;
}
#endif

bool MainWindow::isBonjourRunning()
{
    bool result = false;

#if defined(Q_OS_WIN)
    result = isServiceRunning("Bonjour Service");
#else
    result = true;
#endif

    return result;
}

void MainWindow::showBonjourMissingMessage()
{
#if defined(Q_OS_WIN)
    int r = QMessageBox::information(
        this, tr("InputLeap"),
        tr("Auto config requires the Bonjour service, which is not running "
           "on this computer.\n\n"
           "Auto config is optional: you can instead type the server's IP "
           "address or hostname on the client.\n\n"
           "To use auto config, install \"Bonjour Print Services for "
           "Windows\" from Apple, then restart InputLeap. "
           "Open the download page now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (r == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl(bonjourDownloadUrl));
    }
#endif
}

void MainWindow::promptAutoConfig()
{
    // Auto config is optional. Don't nag on first start when Bonjour is
    // missing; users can still enable it later from the checkbox.
    if (!isBonjourRunning()) {
        m_AppConfig->setAutoConfig(false);
        ui_->m_pCheckBoxAutoConfig->setChecked(false);
    }

    m_AppConfig->setAutoConfigPrompted(true);
}

void MainWindow::comboServerList_currentIndexChanged(QString )
{
    if (ui_->m_pComboServerList->count() != 0) {
        restart_cmd_app();
    }
}

void MainWindow::on_m_pCheckBoxAutoConfig_toggled(bool checked)
{
    if (!isBonjourRunning() && checked) {
        if (!m_SuppressAutoConfigWarning) {
            showBonjourMissingMessage();
        }

        ui_->m_pCheckBoxAutoConfig->setChecked(false);
        return;
    }

    ui_->m_pLineEditHostname->setDisabled(checked);
    appConfig().setAutoConfig(checked);
    updateZeroconfService();

    if (!checked) {
        ui_->m_pComboServerList->clear();
        ui_->m_pComboServerList->hide();
    }
}

void MainWindow::windowStateChanged()
{
    if (windowState() == Qt::WindowMinimized && appConfig().getMinimizeToTray())
        hide();
}

void MainWindow::setupConnectionModeUi()
{
    // the client chooses how to connect; the server accepts both at once
    m_pConnectionModeField = new QWidget(ui_->m_pGroupClient);
    auto* modeLayout = new QHBoxLayout(m_pConnectionModeField);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(0);

    auto makeModeButton = [this](const QString& text, const QString& toolTip, bool bluetooth) {
        auto* button = new QToolButton(m_pConnectionModeField);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setCheckable(true);
        button->setIcon(mode_icon(bluetooth, palette()));
        button->setIconSize(QSize(18, 18));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setObjectName(bluetooth ? "modeBluetooth" : "modeNetwork");
        return button;
    };
    m_pButtonModeNetwork = makeModeButton(tr("Network"),
        tr("Connect over your local network (Wi-Fi or Ethernet)."), false);
    m_pButtonModeBluetooth = makeModeButton(tr("Bluetooth"),
        tr("Connect directly over Bluetooth. Works even when a VPN blocks the local network."),
        true);
    m_pButtonModeNetwork->setChecked(true);

    // make the two buttons look like one segmented control
    m_pConnectionModeField->setStyleSheet(
        "QToolButton { padding: 5px 14px; border: 1px solid palette(mid);"
        "  background: palette(button); }"
        "QToolButton:hover:!checked { background: palette(midlight); }"
        "QToolButton:checked { background: palette(highlight); color: palette(highlighted-text);"
        "  border-color: palette(highlight); }"
        "QToolButton:checked:disabled { background: palette(midlight); color: palette(dark);"
        "  border-color: palette(mid); }"
        "QToolButton#modeNetwork { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }"
        "QToolButton#modeBluetooth { border-left: none; border-top-right-radius: 6px;"
        "  border-bottom-right-radius: 6px; }");

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_pButtonModeNetwork);
    group->addButton(m_pButtonModeBluetooth);

    modeLayout->addWidget(m_pButtonModeNetwork);
    modeLayout->addWidget(m_pButtonModeBluetooth);
    modeLayout->addStretch();

    m_pLabelConnectionMode = new QLabel(tr("Connect over:"), ui_->m_pGroupClient);
    m_pLabelConnectionMode->setBuddy(m_pButtonModeNetwork);
    ui_->formLayout_3->insertRow(0, m_pLabelConnectionMode, m_pConnectionModeField);

    // top of the window: tells the user why some choices are locked
    m_pConnectionModeRow = new QWidget(this);
    auto* managedLayout = new QHBoxLayout(m_pConnectionModeRow);
    managedLayout->setContentsMargins(0, 0, 0, 0);
    m_pLabelManaged = new QLabel(tr("Managed by your organization"), m_pConnectionModeRow);
    QFont managedFont = m_pLabelManaged->font();
    managedFont.setItalic(true);
    m_pLabelManaged->setFont(managedFont);
    QStringList managed;
    if (m_policy.encryption_required()) {
        managed << tr("Encryption is always on.");
    }
    if (m_policy.clipboard_sharing_disabled()) {
        managed << tr("Clipboard sharing is turned off.");
    }
    if (m_policy.file_transfer_disabled()) {
        managed << tr("File transfer is turned off.");
    }
    if (!m_policy.network_allowed()) {
        managed << tr("Connecting over the network is turned off.");
    }
    if (!m_policy.bluetooth_allowed()) {
        managed << tr("Connecting over Bluetooth is turned off.");
    }
    if (m_policy.lock_settings) {
        managed << tr("Settings can't be changed.");
    }
    m_pLabelManaged->setToolTip(tr("Your organization manages some settings on this computer:") +
                                "\n" + managed.join("\n"));
    managedLayout->addWidget(m_pLabelManaged);
    managedLayout->addStretch();

    const QString lockedByPolicy = tr("Turned off by your organization");
    if (!m_policy.network_allowed()) {
        m_pButtonModeNetwork->setEnabled(false);
        m_pButtonModeNetwork->setToolTip(lockedByPolicy);
    }
    if (!m_policy.bluetooth_allowed()) {
        m_pButtonModeBluetooth->setEnabled(false);
        m_pButtonModeBluetooth->setToolTip(lockedByPolicy);
    }
    ui_->verticalLayout_2->insertWidget(0, m_pConnectionModeRow);

    connect(m_pButtonModeNetwork, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            setConnectionMode(ConnectionMode::Network);
        }
    });
    connect(m_pButtonModeBluetooth, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            setConnectionMode(ConnectionMode::Bluetooth);
        }
    });

    const QString pairingHint = tr("Both computers must be paired first in "
                                   "<a href=\"ms-settings:bluetooth\">Bluetooth settings</a>.");

    // server: this computer's Bluetooth address, ready to copy
    m_pLabelBluetoothAddressTitle = new QLabel(tr("Bluetooth address:"), ui_->m_pGroupServer);
    m_pBluetoothAddressField = new QWidget(ui_->m_pGroupServer);
    auto* addressLayout = new QHBoxLayout(m_pBluetoothAddressField);
    addressLayout->setContentsMargins(0, 0, 0, 0);
    m_pLabelBluetoothAddress = new QLabel(m_pBluetoothAddressField);
    m_pLabelBluetoothAddress->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pButtonCopyBluetoothAddress = new QToolButton(m_pBluetoothAddressField);
    m_pButtonCopyBluetoothAddress->setText(tr("Copy"));
    m_pButtonCopyBluetoothAddress->setToolTip(tr("Copy this address to the clipboard"));
    m_pButtonRefreshBluetoothAddress = new QToolButton(m_pBluetoothAddressField);
    m_pButtonRefreshBluetoothAddress->setText(tr("Refresh"));
    m_pButtonRefreshBluetoothAddress->setToolTip(tr("Look for a Bluetooth adapter again"));
    addressLayout->addWidget(m_pLabelBluetoothAddress);
    addressLayout->addWidget(m_pButtonCopyBluetoothAddress);
    addressLayout->addWidget(m_pButtonRefreshBluetoothAddress);
    addressLayout->addStretch();
    ui_->formLayout->addRow(m_pLabelBluetoothAddressTitle, m_pBluetoothAddressField);

    m_pLabelServerBluetoothHint = new QLabel(
        tr("Other computers can connect over the network or Bluetooth. For Bluetooth, pair "
           "them in <a href=\"ms-settings:bluetooth\">Bluetooth settings</a>, then choose this "
           "computer on the other one. If it isn't listed there, enter this address instead."),
        ui_->m_pGroupServer);
    m_pLabelServerBluetoothHint->setWordWrap(true);
    m_pLabelServerBluetoothHint->setOpenExternalLinks(true);
    ui_->formLayout->addRow(m_pLabelServerBluetoothHint);

    connect(m_pButtonCopyBluetoothAddress, &QToolButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_pLabelBluetoothAddress->text());
        m_pButtonCopyBluetoothAddress->setText(tr("Copied"));
        QTimer::singleShot(1500, m_pButtonCopyBluetoothAddress, [this]() {
            m_pButtonCopyBluetoothAddress->setText(tr("Copy"));
        });
    });
    connect(m_pButtonRefreshBluetoothAddress, &QToolButton::clicked,
            this, &MainWindow::updateLocalBluetoothAddress);

    // client: pick the server from the paired computers, or type its address
    m_pLabelServerBluetoothTitle = new QLabel(tr("Server:"), ui_->m_pGroupClient);
    m_pBluetoothServerField = new QWidget(ui_->m_pGroupClient);
    auto* serverLayout = new QHBoxLayout(m_pBluetoothServerField);
    serverLayout->setContentsMargins(0, 0, 0, 0);
    m_pComboBluetoothServer = new QComboBox(m_pBluetoothServerField);
    m_pComboBluetoothServer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pButtonRefreshBluetoothServers = new QToolButton(m_pBluetoothServerField);
    m_pButtonRefreshBluetoothServers->setText(tr("Refresh"));
    m_pButtonRefreshBluetoothServers->setToolTip(tr("Look for paired computers again"));
    serverLayout->addWidget(m_pComboBluetoothServer);
    serverLayout->addWidget(m_pButtonRefreshBluetoothServers);
    m_pLabelServerBluetoothTitle->setBuddy(m_pComboBluetoothServer);
    ui_->formLayout_3->addRow(m_pLabelServerBluetoothTitle, m_pBluetoothServerField);

    m_pLabelServerBluetoothManualTitle = new QLabel(tr("Server's address:"), ui_->m_pGroupClient);
    m_pLineEditServerBluetooth = new QLineEdit(ui_->m_pGroupClient);
    m_pLineEditServerBluetooth->setPlaceholderText(tr("e.g. 00:28:F8:8F:56:C3"));
    m_pLabelServerBluetoothManualTitle->setBuddy(m_pLineEditServerBluetooth);
    ui_->formLayout_3->addRow(m_pLabelServerBluetoothManualTitle, m_pLineEditServerBluetooth);

    connect(m_pComboBluetoothServer, QOverload<int>::of(&QComboBox::activated),
            this, &MainWindow::selectBluetoothServer);
    connect(m_pButtonRefreshBluetoothServers, &QToolButton::clicked,
            this, &MainWindow::refreshPairedBluetoothServers);

    m_pLabelClientBluetoothHint = new QLabel(
        tr("Choose the computer that shares its keyboard and mouse.") + " " + pairingHint,
        ui_->m_pGroupClient);
    m_pLabelClientBluetoothHint->setWordWrap(true);
    m_pLabelClientBluetoothHint->setOpenExternalLinks(true);
    ui_->formLayout_3->addRow(m_pLabelClientBluetoothHint);

    connect(m_pLineEditServerBluetooth, &QLineEdit::editingFinished, this, [this]() {
        QString normalized = inputleap::normalize_bluetooth_address(m_pLineEditServerBluetooth->text());
        if (!normalized.isEmpty()) {
            m_pLineEditServerBluetooth->setText(normalized);
        }
    });

    const bool bluetoothSupported = inputleap::is_bluetooth_supported();
    m_pLabelConnectionMode->setVisible(bluetoothSupported);
    m_pConnectionModeField->setVisible(bluetoothSupported);
    m_pConnectionModeRow->setVisible(m_policy.any());
}

void MainWindow::setConnectionMode(ConnectionMode mode)
{
    if (mode == m_ConnectionMode) {
        return;
    }
    m_ConnectionMode = mode;
    updateConnectionModeUi();
    saveSettings();

    // the client must be restarted to switch transport
    if (m_ExpectedRunningState == kStarted && app_role() == AppRole::Client) {
        restart_cmd_app();
    }
}

void MainWindow::updateConnectionModeUi()
{
    bool bluetooth = m_ConnectionMode == ConnectionMode::Bluetooth;

    // server: shows every way the clients can reach it
    const bool serverNetwork = m_policy.network_allowed();
    const bool serverBluetooth = server_accepts_bluetooth();
    ui_->label_2->setVisible(serverNetwork);
    ui_->m_pLabelIpAddresses->setVisible(serverNetwork);
    m_pLabelBluetoothAddressTitle->setVisible(serverBluetooth);
    m_pBluetoothAddressField->setVisible(serverBluetooth);
    m_pLabelServerBluetoothHint->setVisible(serverBluetooth);
    if (serverBluetooth) {
        updateLocalBluetoothAddress();
    }

    // client
    ui_->m_pLabelServerName->setVisible(!bluetooth);
    ui_->m_pLineEditHostname->setVisible(!bluetooth);
    ui_->m_pCheckBoxAutoConfig->setVisible(!bluetooth);
    ui_->m_pComboServerList->setVisible(!bluetooth && ui_->m_pComboServerList->count() > 1);
    m_pLabelServerBluetoothTitle->setVisible(bluetooth);
    m_pBluetoothServerField->setVisible(bluetooth);
    m_pLabelServerBluetoothManualTitle->setVisible(false);
    m_pLineEditServerBluetooth->setVisible(false);
    m_pLabelClientBluetoothHint->setVisible(bluetooth);

    if (bluetooth) {
        refreshPairedBluetoothServers();
    }
}

bool MainWindow::server_accepts_bluetooth() const
{
    return inputleap::is_bluetooth_supported() && m_policy.bluetooth_allowed();
}

namespace {
const char BLUETOOTH_MANUAL_ENTRY[] = "manual";
}

void MainWindow::refreshPairedBluetoothServers()
{
    const QString current =
            inputleap::normalize_bluetooth_address(m_pLineEditServerBluetooth->text());
    const auto devices = inputleap::paired_bluetooth_computers();

    m_pComboBluetoothServer->clear();
    for (const auto& device : devices) {
        m_pComboBluetoothServer->addItem(device.name, device.address);
        m_pComboBluetoothServer->setItemData(m_pComboBluetoothServer->count() - 1,
                                             device.address, Qt::ToolTipRole);
    }
    m_pComboBluetoothServer->addItem(tr("Enter address manually..."),
                                     QString(BLUETOOTH_MANUAL_ENTRY));

    // keep the saved server selected; an address that is not paired (or no
    // paired computers at all) falls back to typing it in
    int index = current.isEmpty() ? -1 : m_pComboBluetoothServer->findData(current);
    if (index < 0 && (devices.isEmpty() || !current.isEmpty())) {
        index = m_pComboBluetoothServer->count() - 1;
    }
    if (index < 0) {
        index = 0;
    }
    m_pComboBluetoothServer->setCurrentIndex(index);
    selectBluetoothServer(index);

    if (devices.isEmpty()) {
        return;
    }

    // ask each paired computer in the background whether the server is running
    const int generation = ++m_BluetoothServerScan;
    QStringList addresses;
    for (const auto& device : devices) {
        addresses << device.address;
    }
    QPointer<MainWindow> self(this);
    QThread* thread = QThread::create([self, generation, addresses]() {
        QMap<QString, bool> running;
        for (const auto& address : addresses) {
            running[address] = inputleap::is_server_running_on(address);
        }
        QMetaObject::invokeMethod(qApp, [self, generation, running]() {
            if (self) {
                self->applyBluetoothServerStatus(generation, running);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::applyBluetoothServerStatus(int generation, const QMap<QString, bool>& running)
{
    if (generation != m_BluetoothServerScan) {
        return; // the list was refreshed since this check started
    }
    for (int i = 0; i < m_pComboBluetoothServer->count(); ++i) {
        const QString address = m_pComboBluetoothServer->itemData(i).toString();
        if (!running.contains(address)) {
            continue;
        }
        QString name = m_pComboBluetoothServer->itemText(i);
        name = running[address] ? tr("%1 (ready)").arg(name) : tr("%1 (not running)").arg(name);
        m_pComboBluetoothServer->setItemText(i, name);
    }
}

void MainWindow::selectBluetoothServer(int index)
{
    const QString data = m_pComboBluetoothServer->itemData(index).toString();
    const bool manual = data == BLUETOOTH_MANUAL_ENTRY;
    const bool bluetooth = m_ConnectionMode == ConnectionMode::Bluetooth;

    if (!manual) {
        m_pLineEditServerBluetooth->setText(data);
    }
    m_pLabelServerBluetoothManualTitle->setVisible(bluetooth && manual);
    m_pLineEditServerBluetooth->setVisible(bluetooth && manual);
    if (manual && bluetooth && isVisible()) {
        m_pLineEditServerBluetooth->setFocus();
    }
}

void MainWindow::updateLocalBluetoothAddress()
{
    QString address = inputleap::local_bluetooth_address();
    bool found = !address.isEmpty();

    if (found) {
        m_pLabelBluetoothAddress->setText(address);
        QFont font = m_pLabelBluetoothAddress->font();
        font.setBold(true);
        m_pLabelBluetoothAddress->setFont(font);
    } else {
        m_pLabelBluetoothAddress->setText(tr("Not found. Is Bluetooth turned on?"));
        m_pLabelBluetoothAddress->setFont(QFont());
    }
    m_pButtonCopyBluetoothAddress->setVisible(found);
    m_pButtonRefreshBluetoothAddress->setVisible(!found);
}

void MainWindow::showLogWindow()
{
    m_pLogWindow->show();
}
