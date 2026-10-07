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

// The sidebar's own pages: Clipboard & files, and Security. They open next
// to the sidebar like Settings and the activity log.

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "AppConfig.h"
#include "common/DataDirectories.h"
#include "net/FingerprintDatabase.h"
#include "net/SecureUtils.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QLabel* section_title(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "cardTitle");
    return label;
}

QLabel* muted(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "muted");
    label->setWordWrap(true);
    return label;
}

// A switch-like row: a bold label, a line of explanation and a checkbox.
QCheckBox* option_row(QVBoxLayout* layout, QWidget* parent, const QString& title,
                      const QString& detail)
{
    auto* row = new QHBoxLayout();
    row->setSpacing(14);
    auto* words = new QVBoxLayout();
    words->setSpacing(2);
    auto* heading = new QLabel(title, parent);
    heading->setProperty("role", "strong");
    words->addWidget(heading);
    words->addWidget(muted(detail, parent));
    auto* box = new QCheckBox(parent);
    box->setProperty("switch", true);
    box->setCursor(Qt::PointingHandCursor);
    box->setAccessibleName(title);
    row->addLayout(words, 1);
    row->addWidget(box, 0, Qt::AlignVCenter);
    layout->addLayout(row);
    return box;
}

QString short_fingerprint(const glidekvm::FingerprintData& fingerprint)
{
    auto text = QString::fromStdString(glidekvm::format_ssl_fingerprint(fingerprint.data));
    return text.left(29) + QStringLiteral(" ...");
}

} // namespace

void MainWindow::addSharingOptions(QVBoxLayout* layout, QWidget* parent, QCheckBox** clipboard,
                                   QCheckBox** files)
{
    *clipboard = option_row(layout, parent, tr("Shared clipboard"),
                            tr("Copy on one computer, paste on the other."));
    *files = option_row(layout, parent, tr("Drag files across"),
                        tr("Drag a file off the edge of the screen to copy it to the other "
                           "computer."));
    connect(*clipboard, &QCheckBox::toggled, this, [this](bool on) { setSharingOption(true, on); });
    connect(*files, &QCheckBox::toggled, this, [this](bool on) { setSharingOption(false, on); });
    syncSharingOptions(*clipboard, *files);
}

void MainWindow::syncSharingOptions(QCheckBox* clipboard, QCheckBox* files)
{
    if (clipboard == nullptr || files == nullptr) {
        return;
    }
    const bool server = app_role() == AppRole::Server;
    const QString managed = tr("Turned off by your organization.");
    const QString decided_by_server = tr("Your main computer decides this.");

    QSignalBlocker block_clipboard(clipboard);
    QSignalBlocker block_files(files);
    const bool clipboard_locked = m_policy.clipboard_sharing_disabled();
    const bool files_locked = m_policy.file_transfer_disabled();
    clipboard->setChecked(!clipboard_locked && serverConfig().clipboardSharing());
    files->setChecked(!files_locked && serverConfig().enableDragAndDrop());
    clipboard->setEnabled(server && !clipboard_locked);
    files->setEnabled(server && !files_locked);
    clipboard->setToolTip(clipboard_locked ? managed : server ? QString() : decided_by_server);
    files->setToolTip(files_locked ? managed : server ? QString() : decided_by_server);
}

void MainWindow::setSharingOption(bool clipboard, bool on)
{
    if (clipboard) {
        serverConfig().setClipboardSharing(on);
    } else {
        serverConfig().setEnableDragAndDrop(on);
    }
    serverConfig().saveSettings();
    syncSharingOptions(m_pHomeShareClipboard, m_pHomeShareFiles);
    syncSharingOptions(m_pPageShareClipboard, m_pPageShareFiles);

    // the server reads its options when it starts
    if (m_ExpectedRunningState == kStarted && app_role() == AppRole::Server) {
        restart_cmd_app();
    }
}

void MainWindow::showClipboardPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(16);

    layout->addWidget(section_title(tr("Between computers"), page));
    QCheckBox* clipboard = nullptr;
    QCheckBox* files = nullptr;
    addSharingOptions(layout, page, &clipboard, &files);
    // guarded pointers: the page is deleted when another one opens
    m_pPageShareClipboard = clipboard;
    m_pPageShareFiles = files;
    if (app_role() != AppRole::Server) {
        layout->addWidget(muted(tr("These are set on the computer whose keyboard and mouse "
                                   "you share."), page));
    }
    if (m_policy.clipboard_sharing_disabled() || m_policy.file_transfer_disabled()) {
        layout->addWidget(muted(tr("Some of these are managed by your organization."), page));
    }
    layout->addStretch();

    showPanel(tr("Clipboard & files"), page, m_pNavClipboard);
}

void MainWindow::showSecurityPage()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(12);

    // encryption
    layout->addWidget(section_title(tr("Encryption"), page));
    QString encryption;
    if (!appConfig().getCryptoEnabled()) {
        encryption = tr("Encryption is off. Turn it on in Settings so keystrokes are never "
                        "sent in the clear.");
    } else if (m_policy.encryption_required()) {
        encryption = tr("Connections are encrypted with TLS. Your organization requires it.");
    } else {
        encryption = tr("Connections are encrypted with TLS. Bluetooth links must also be "
                        "paired.");
    }
    layout->addWidget(muted(encryption, page));

    // this computer's fingerprint
    layout->addSpacing(8);
    layout->addWidget(section_title(tr("This computer's fingerprint"), page));
    layout->addWidget(muted(tr("The first time another computer connects, it shows this "
                               "fingerprint. Check that the two match."), page));
    glidekvm::FingerprintDatabase local;
    const auto local_path = glidekvm::DataDirectories::local_ssl_fingerprints_path();
    if (appConfig().getCryptoEnabled() && glidekvm::fs::exists(local_path)) {
        local.read(local_path);
    }
    bool shown = false;
    for (const auto& fingerprint : local.fingerprints()) {
        if (fingerprint.algorithm != "sha256") {
            continue;
        }
        const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        auto* row = new QHBoxLayout();
        row->setSpacing(16);
        auto* columns = new QLabel(
            QString::fromStdString(glidekvm::format_ssl_fingerprint_columns(fingerprint.data)), page);
        columns->setFont(mono);
        columns->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* art = new QLabel(
            QString::fromStdString(glidekvm::create_fingerprint_randomart(fingerprint.data)), page);
        art->setFont(mono);
        row->addWidget(columns);
        row->addWidget(art);
        row->addStretch();
        layout->addLayout(row);

        const QString full = QString::fromStdString(glidekvm::format_ssl_fingerprint(fingerprint.data));
        auto* copy = new QPushButton(tr("Copy fingerprint"), page);
        connect(copy, &QPushButton::clicked, this, [copy, full]() {
            QGuiApplication::clipboard()->setText(full);
            copy->setText(QObject::tr("Copied"));
            QTimer::singleShot(1500, copy, [copy]() { copy->setText(QObject::tr("Copy fingerprint")); });
        });
        auto* copyRow = new QHBoxLayout();
        copyRow->addWidget(copy);
        copyRow->addStretch();
        layout->addLayout(copyRow);
        shown = true;
    }
    if (!shown) {
        layout->addWidget(muted(tr("Not available while encryption is off."), page));
    }

    // computers this one trusts
    layout->addSpacing(8);
    layout->addWidget(section_title(tr("Trusted computers"), page));
    layout->addWidget(muted(tr("Computers whose fingerprint you confirmed. Forget one and you "
                               "will be asked to confirm it again next time it connects."), page));
    struct TrustList {
        glidekvm::fs::path path;
        QString kind;
    };
    const TrustList lists[] = {
        {glidekvm::DataDirectories::trusted_clients_ssl_fingerprints_path(),
         tr("Uses this keyboard and mouse")},
        {glidekvm::DataDirectories::trusted_servers_ssl_fingerprints_path(),
         tr("Main computer")},
    };
    int count = 0;
    for (const auto& list : lists) {
        if (!glidekvm::fs::exists(list.path)) {
            continue;
        }
        glidekvm::FingerprintDatabase db;
        db.read(list.path);
        for (const auto& fingerprint : db.fingerprints()) {
            if (fingerprint.algorithm != "sha256") {
                continue;
            }
            auto* tile = new QFrame(page);
            tile->setProperty("tile", true);
            auto* row = new QHBoxLayout(tile);
            row->setContentsMargins(14, 10, 14, 10);
            auto* words = new QVBoxLayout();
            words->setSpacing(2);
            auto* name = new QLabel(short_fingerprint(fingerprint), tile);
            name->setProperty("role", "strong");
            words->addWidget(name);
            words->addWidget(muted(list.kind, tile));
            auto* forget = new QPushButton(tr("Forget"), tile);
            row->addLayout(words, 1);
            row->addWidget(forget);
            layout->addWidget(tile);

            const auto path = list.path;
            const auto line = glidekvm::FingerprintDatabase::to_db_line(fingerprint);
            connect(forget, &QPushButton::clicked, this, [this, path, line]() {
                glidekvm::FingerprintDatabase all;
                all.read(path);
                glidekvm::FingerprintDatabase kept;
                for (const auto& entry : all.fingerprints()) {
                    if (glidekvm::FingerprintDatabase::to_db_line(entry) != line) {
                        kept.add_trusted(entry);
                    }
                }
                kept.write(path);
                // rebuild the page so the list shows what is left
                showSecurityPage();
            });
            ++count;
        }
    }
    if (count == 0) {
        layout->addWidget(muted(tr("No trusted computers yet."), page));
    }
    layout->addStretch();

    showPanel(tr("Security"), page, m_pNavSecurity);
}

QString MainWindow::clientSide(const QString& name) const
{
    const auto& screens = m_ServerConfig.screens();
    const int columns = m_ServerConfig.numColumns();
    int self = -1;
    int other = -1;
    for (int i = 0; i < static_cast<int>(screens.size()); ++i) {
        if (screens[i].isNull()) {
            continue;
        }
        if (screens[i].name() == m_AppConfig->screenName()) {
            self = i;
        } else if (screens[i].name() == name) {
            other = i;
        }
    }
    if (self < 0 || other < 0 || columns <= 0) {
        return QString();
    }
    const int dx = other % columns - self % columns;
    const int dy = other / columns - self / columns;
    if (dy == 0) {
        return dx > 0 ? tr("to the right") : tr("to the left");
    }
    if (dx == 0) {
        return dy > 0 ? tr("below") : tr("above");
    }
    return QString();
}
