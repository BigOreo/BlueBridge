/*
 * GlideKVM -- mouse and keyboard sharing utility
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

#include "ServerConfigDialog.h"
#include <ui_ServerConfigDialog.h>

#include "ServerConfig.h"
#include "HotkeyDialog.h"
#include "ActionDialog.h"
#include "DeskView.h"
#include "KeySequenceWidget.h"
#include "ScreenSettingsDialog.h"
#include "Theme.h"

#include <QtCore>
#include <QtGui>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "common/Policy.h"

namespace {

QLabel* muted_label(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "muted");
    label->setWordWrap(true);
    return label;
}

QLabel* strong_label(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "strong");
    return label;
}

// the neighbouring places, in the order the "Sits to the" list offers them
struct Side {
    int dx;
    int dy;
    const char* text;
};
const Side kSides[] = {
    {1, 0, QT_TRANSLATE_NOOP("ServerConfigDialog", "Right of %1")},
    {-1, 0, QT_TRANSLATE_NOOP("ServerConfigDialog", "Left of %1")},
    {0, -1, QT_TRANSLATE_NOOP("ServerConfigDialog", "Above %1")},
    {0, 1, QT_TRANSLATE_NOOP("ServerConfigDialog", "Below %1")},
};

} // namespace

ServerConfigDialog::ServerConfigDialog(QWidget* parent, ServerConfig& config, const QString& defaultScreenName) :
    QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
    ui_{std::make_unique<Ui::ServerConfigDialog>()},
    m_OrigServerConfig(config),
    m_ServerConfig(config),
    m_Message(""),
    m_ServerName(defaultScreenName)
{
    ui_->setupUi(this);

    ui_->m_pCheckBoxHeartbeat->setChecked(serverConfig().hasHeartbeat());
    ui_->m_pSpinBoxHeartbeat->setValue(serverConfig().heartbeat());

    ui_->m_pCheckBoxRelativeMouseMoves->setChecked(serverConfig().relativeMouseMoves());
    ui_->m_pCheckBoxScreenSaverSync->setChecked(serverConfig().screenSaverSync());
    ui_->m_pCheckBoxWin32KeepForeground->setChecked(serverConfig().win32KeepForeground());

    ui_->m_pCheckBoxSwitchDelay->setChecked(serverConfig().hasSwitchDelay());
    ui_->m_pSpinBoxSwitchDelay->setValue(serverConfig().switchDelay());

    ui_->m_pCheckBoxSwitchDoubleTap->setChecked(serverConfig().hasSwitchDoubleTap());
    ui_->m_pSpinBoxSwitchDoubleTap->setValue(serverConfig().switchDoubleTap());

    ui_->m_pCheckBoxCornerTopLeft->setChecked(serverConfig().switchCorner(BaseConfig::SwitchCorner::TopLeft));
    ui_->m_pCheckBoxCornerTopRight->setChecked(serverConfig().switchCorner(BaseConfig::SwitchCorner::TopRight));
    ui_->m_pCheckBoxCornerBottomLeft->setChecked(serverConfig().switchCorner(BaseConfig::SwitchCorner::BottomLeft));
    ui_->m_pCheckBoxCornerBottomRight->setChecked(serverConfig().switchCorner(BaseConfig::SwitchCorner::BottomRight));
    ui_->m_pSpinBoxSwitchCornerSize->setValue(serverConfig().switchCornerSize());

    ui_->m_pCheckBoxIgnoreAutoConfigClient->setChecked(serverConfig().ignoreAutoConfigClient());

    ui_->m_pCheckBoxEnableDragAndDrop->setChecked(serverConfig().enableDragAndDrop());

    ui_->m_pCheckBoxEnableClipboard->setChecked(serverConfig().clipboardSharing());
    ui_->m_pSpinBoxClipboardSizeLimit->setValue(serverConfig().clipboardSharingSize());
    ui_->m_pSpinBoxClipboardSizeLimit->setEnabled(serverConfig().clipboardSharing());

    const auto policy = glidekvm::read_machine_policy();
    const QString managed = tr("Turned off by your organization");
    if (policy.file_transfer_disabled()) {
        ui_->m_pCheckBoxEnableDragAndDrop->setChecked(false);
        ui_->m_pCheckBoxEnableDragAndDrop->setEnabled(false);
        ui_->m_pCheckBoxEnableDragAndDrop->setToolTip(managed);
    }
    if (policy.clipboard_sharing_disabled()) {
        ui_->m_pCheckBoxEnableClipboard->setChecked(false);
        ui_->m_pCheckBoxEnableClipboard->setEnabled(false);
        ui_->m_pCheckBoxEnableClipboard->setToolTip(managed);
        ui_->m_pSpinBoxClipboardSizeLimit->setEnabled(false);
    }

    for (const Hotkey& hotkey : serverConfig().hotkeys()) {
        ui_->m_pListHotkeys->addItem(hotkey.text());
    }

    if (serverConfig().numScreens() == 0) {
        const int middle = serverConfig().numRows() / 2 * serverConfig().numColumns() +
                           serverConfig().numColumns() / 2;
        serverConfig().screens()[middle] = Screen(defaultScreenName);
    }

    buildLayoutTab();
}

void ServerConfigDialog::accept()
{
    serverConfig().haveHeartbeat(ui_->m_pCheckBoxHeartbeat->isChecked());
    serverConfig().setHeartbeat(ui_->m_pSpinBoxHeartbeat->value());

    serverConfig().setRelativeMouseMoves(ui_->m_pCheckBoxRelativeMouseMoves->isChecked());
    serverConfig().setScreenSaverSync(ui_->m_pCheckBoxScreenSaverSync->isChecked());
    serverConfig().setWin32KeepForeground(ui_->m_pCheckBoxWin32KeepForeground->isChecked());

    serverConfig().haveSwitchDelay(ui_->m_pCheckBoxSwitchDelay->isChecked());
    serverConfig().setSwitchDelay(ui_->m_pSpinBoxSwitchDelay->value());

    serverConfig().haveSwitchDoubleTap(ui_->m_pCheckBoxSwitchDoubleTap->isChecked());
    serverConfig().setSwitchDoubleTap(ui_->m_pSpinBoxSwitchDoubleTap->value());

    serverConfig().setSwitchCorner(BaseConfig::SwitchCorner::TopLeft,
                                   ui_->m_pCheckBoxCornerTopLeft->isChecked());
    serverConfig().setSwitchCorner(BaseConfig::SwitchCorner::TopRight,
                                   ui_->m_pCheckBoxCornerTopRight->isChecked());
    serverConfig().setSwitchCorner(BaseConfig::SwitchCorner::BottomLeft,
                                   ui_->m_pCheckBoxCornerBottomLeft->isChecked());
    serverConfig().setSwitchCorner(BaseConfig::SwitchCorner::BottomRight,
                                   ui_->m_pCheckBoxCornerBottomRight->isChecked());
    serverConfig().setSwitchCornerSize(ui_->m_pSpinBoxSwitchCornerSize->value());
    serverConfig().setIgnoreAutoConfigClient(ui_->m_pCheckBoxIgnoreAutoConfigClient->isChecked());
    serverConfig().setEnableDragAndDrop(ui_->m_pCheckBoxEnableDragAndDrop->isChecked());
    serverConfig().setClipboardSharing(ui_->m_pCheckBoxEnableClipboard->isChecked());
    serverConfig().setClipboardSharingSize(ui_->m_pSpinBoxClipboardSizeLimit->value());
    serverConfig().setSwitchNeedsControl(m_pCheckNeedsControl->isChecked());

    // now that the dialog has been accepted, copy the new server config to the original one,
    // which is a reference to the one in MainWindow.
    setOrigServerConfig(serverConfig());

    QDialog::accept();
}

void ServerConfigDialog::on_m_pButtonNewHotkey_clicked()
{
    Hotkey hotkey;
    HotkeyDialog dlg(this, hotkey);
    if (dlg.exec() == QDialog::Accepted)
    {
        serverConfig().hotkeys().push_back(hotkey);
        ui_->m_pListHotkeys->addItem(hotkey.text());
    }
}

void ServerConfigDialog::on_m_pButtonEditHotkey_clicked()
{
    int idx = ui_->m_pListHotkeys->currentRow();
    Q_ASSERT(idx >= 0 && idx < static_cast<int>(serverConfig().hotkeys().size()));
    Hotkey& hotkey = serverConfig().hotkeys()[idx];
    HotkeyDialog dlg(this, hotkey);
    if (dlg.exec() == QDialog::Accepted)
        ui_->m_pListHotkeys->currentItem()->setText(hotkey.text());
}

void ServerConfigDialog::on_m_pButtonRemoveHotkey_clicked()
{
    int idx = ui_->m_pListHotkeys->currentRow();
    Q_ASSERT(idx >= 0 && idx < static_cast<int>(serverConfig().hotkeys().size()));
    serverConfig().hotkeys().erase(serverConfig().hotkeys().begin() + idx);
    ui_->m_pListActions->clear();
    delete ui_->m_pListHotkeys->item(idx);
}

void ServerConfigDialog::on_m_pListHotkeys_itemSelectionChanged()
{
    bool itemsSelected = !ui_->m_pListHotkeys->selectedItems().isEmpty();
    ui_->m_pButtonEditHotkey->setEnabled(itemsSelected);
    ui_->m_pButtonRemoveHotkey->setEnabled(itemsSelected);
    ui_->m_pButtonNewAction->setEnabled(itemsSelected);

    if (itemsSelected && serverConfig().hotkeys().size() > 0)
    {
        ui_->m_pListActions->clear();

        int idx = ui_->m_pListHotkeys->row(ui_->m_pListHotkeys->selectedItems()[0]);

        // There's a bug somewhere around here: We get idx == 1 right after we deleted the next to last item, so idx can
        // only possibly be 0. GDB shows we got called indirectly from the delete line in
        // on_m_pButtonRemoveHotkey_clicked() above, but the delete is of course necessary and seems correct.
        // The while() is a generalized workaround for all that and shouldn't be required.
        while (idx >= 0 && idx >= static_cast<int>(serverConfig().hotkeys().size()))
            idx--;

        Q_ASSERT(idx >= 0 && idx < static_cast<int>(serverConfig().hotkeys().size()));

        const Hotkey& hotkey = serverConfig().hotkeys()[idx];
        for (const Action& action : hotkey.actions()) {
            ui_->m_pListActions->addItem(action.text());
        }
    }
}

void ServerConfigDialog::on_m_pButtonNewAction_clicked()
{
    int idx = ui_->m_pListHotkeys->currentRow();
    Q_ASSERT(idx >= 0 && idx < static_cast<int>(serverConfig().hotkeys().size()));
    Hotkey& hotkey = serverConfig().hotkeys()[idx];

    Action action;
    ActionDialog dlg(this, serverConfig(), hotkey, action);
    if (dlg.exec() == QDialog::Accepted)
    {
        hotkey.appendAction(action);
        ui_->m_pListActions->addItem(action.text());
    }
}

void ServerConfigDialog::on_m_pButtonEditAction_clicked()
{
    int idxHotkey = ui_->m_pListHotkeys->currentRow();
    Q_ASSERT(idxHotkey >= 0 && idxHotkey < static_cast<int>(serverConfig().hotkeys().size()));
    Hotkey& hotkey = serverConfig().hotkeys()[idxHotkey];

    int idxAction = ui_->m_pListActions->currentRow();
    Q_ASSERT(idxAction >= 0 && idxAction < static_cast<int>(hotkey.actions().size()));
    Action action = hotkey.actions()[idxAction];

    ActionDialog dlg(this, serverConfig(), hotkey, action);
    if (dlg.exec() == QDialog::Accepted) {
        hotkey.setAction(idxAction, action);
        ui_->m_pListActions->currentItem()->setText(action.text());
    }
}

void ServerConfigDialog::on_m_pButtonRemoveAction_clicked()
{
    int idxHotkey = ui_->m_pListHotkeys->currentRow();
    Q_ASSERT(idxHotkey >= 0 && idxHotkey < static_cast<int>(serverConfig().hotkeys().size()));
    Hotkey& hotkey = serverConfig().hotkeys()[idxHotkey];

    int idxAction = ui_->m_pListActions->currentRow();
    Q_ASSERT(idxAction >= 0 && idxAction < static_cast<int>(hotkey.actions().size()));

    hotkey.removeAction(idxAction);
    delete ui_->m_pListActions->currentItem();
}

void ServerConfigDialog::on_m_pListActions_itemSelectionChanged()
{
    ui_->m_pButtonEditAction->setEnabled(!ui_->m_pListActions->selectedItems().isEmpty());
    ui_->m_pButtonRemoveAction->setEnabled(!ui_->m_pListActions->selectedItems().isEmpty());
}

void ServerConfigDialog::on_m_pCheckBoxEnableClipboard_stateChanged(int state)
{
    ui_->m_pSpinBoxClipboardSizeLimit->setEnabled(state == Qt::Checked);
}

void ServerConfigDialog::setComputers(const QStringList& connected, const QStringList& waiting)
{
    m_Connected = connected;
    m_pDesk->setConnected(connected);
    for (const QString& name : waiting) {
        if (!m_Waiting.contains(name)) {
            m_Waiting << name;
        }
    }
    updateWaiting();
    updateSidePanel();
}

void ServerConfigDialog::buildLayoutTab()
{
    // Undo, Cancel and Save sit next to the tabs
    auto* corner = new QWidget(this);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 6);
    cornerLayout->setSpacing(8);
    m_pButtonUndo = new QPushButton(tr("&Undo"), corner);
    m_pButtonUndo->setEnabled(false);
    connect(m_pButtonUndo, &QPushButton::clicked, this, [this]() { undo(); });
    cornerLayout->addWidget(m_pButtonUndo);
    layout()->removeWidget(ui_->m_pButtonBox);
    ui_->m_pButtonBox->setParent(corner);
    cornerLayout->addWidget(ui_->m_pButtonBox);
    for (QAbstractButton* button : ui_->m_pButtonBox->buttons()) {
        button->setIcon(QIcon());
    }
    ui_->m_pTabWidget->setCornerWidget(corner, Qt::TopRightCorner);
    connect(ui_->m_pTabWidget, &QTabWidget::currentChanged, this, [this]() { updateSidePanel(); });

    auto* tab = ui_->m_pTabScreens;
    auto* columns = new QHBoxLayout(tab);
    columns->setContentsMargins(0, 16, 0, 0);
    columns->setSpacing(20);

    // the desk
    auto* deskColumn = new QVBoxLayout();
    deskColumn->setSpacing(12);
    deskColumn->addWidget(muted_label(tr("Drag each computer to where it sits on your desk. "
                                         "The mouse crosses wherever two computers touch."), tab));
    m_pDesk = new DeskView(tab);
    m_pDesk->setServerName(m_ServerName);
    m_pDesk->setScreens(&serverConfig().screens(), serverConfig().numColumns(), serverConfig().numRows());
    deskColumn->addWidget(m_pDesk);

    auto* legend = new QHBoxLayout();
    legend->setSpacing(8);
    auto* swatch = new QLabel(tab);
    swatch->setFixedSize(18, 6);
    swatch->setStyleSheet(QString("background: %1; border-radius: 3px;").arg(glidekvm::theme::kAmber));
    legend->addWidget(swatch);
    legend->addWidget(muted_label(tr("Where the mouse crosses"), tab));
    legend->addStretch();
    m_pCheckNeedsControl = new QCheckBox(tr("Only cross while holding Ctrl"), tab);
    m_pCheckNeedsControl->setProperty("switch", true);
    m_pCheckNeedsControl->setCursor(Qt::PointingHandCursor);
    m_pCheckNeedsControl->setToolTip(tr("Stops the mouse from slipping to another computer by "
                                        "accident. Applies to every edge."));
    m_pCheckNeedsControl->setChecked(serverConfig().switchNeedsControl());
    legend->addWidget(m_pCheckNeedsControl);
    deskColumn->addLayout(legend);

    auto* line = new QFrame(tab);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #E3E8EF;");
    deskColumn->addWidget(line);

    auto* waitingTitle = new QHBoxLayout();
    waitingTitle->addWidget(strong_label(tr("Waiting to be placed"), tab));
    waitingTitle->addStretch();
    auto* add = new QPushButton(tr("Add a computer..."), tab);
    connect(add, &QPushButton::clicked, this, [this]() { addComputer(); });
    waitingTitle->addWidget(add);
    deskColumn->addLayout(waitingTitle);
    auto* waitingRow = new QHBoxLayout();
    waitingRow->setSpacing(8);
    m_pWaitingChips = new QHBoxLayout();
    m_pWaitingChips->setSpacing(8);
    waitingRow->addLayout(m_pWaitingChips);
    m_pLabelWaitingHint = muted_label(QString(), tab);
    waitingRow->addWidget(m_pLabelWaitingHint, 1);
    deskColumn->addLayout(waitingRow);
    deskColumn->addStretch();
    columns->addLayout(deskColumn, 1);

    // the selected computer
    auto* side = new QFrame(tab);
    side->setProperty("card", true);
    side->setFixedWidth(240);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(20, 20, 20, 20);
    m_pSideStack = new QStackedWidget(side);
    sideLayout->addWidget(m_pSideStack);

    auto* empty = new QWidget(m_pSideStack);
    auto* emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->addWidget(muted_label(tr("Select a computer on the desk to change where it sits."),
                                       empty));
    emptyLayout->addStretch();
    m_pSideStack->addWidget(empty);

    auto* details = new QWidget(m_pSideStack);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(8);

    auto* header = new QHBoxLayout();
    header->setSpacing(12);
    m_pSideIcon = new QLabel(details);
    m_pSideIcon->setFixedSize(44, 44);
    m_pSideIcon->setAlignment(Qt::AlignCenter);
    m_pSideIcon->setStyleSheet(QString("background: %1; border-radius: 12px;").arg(glidekvm::theme::kBlueTint));
    header->addWidget(m_pSideIcon);
    auto* words = new QVBoxLayout();
    words->setSpacing(2);
    m_pSideName = new QLabel(details);
    m_pSideName->setProperty("role", "cardTitle");
    m_pSideStatus = muted_label(QString(), details);
    words->addWidget(m_pSideName);
    words->addWidget(m_pSideStatus);
    header->addLayout(words, 1);
    detailsLayout->addLayout(header);
    detailsLayout->addSpacing(8);

    auto* sitsLabel = strong_label(tr("Sits to the"), details);
    detailsLayout->addWidget(sitsLabel);
    m_pComboSide = new QComboBox(details);
    sitsLabel->setBuddy(m_pComboSide);
    connect(m_pComboSide, QOverload<int>::of(&QComboBox::activated), this, [this](int row) {
        moveSelected(m_pComboSide->itemData(row).toInt());
    });
    detailsLayout->addWidget(m_pComboSide);
    m_pLabelUnreachable = new QLabel(tr("The mouse can't reach this computer. Place it next to "
                                        "another one."), details);
    m_pLabelUnreachable->setWordWrap(true);
    m_pLabelUnreachable->setStyleSheet("color: #9A5B00;");
    detailsLayout->addWidget(m_pLabelUnreachable);
    detailsLayout->addSpacing(8);

    m_pShortcutRow = new QWidget(details);
    auto* shortcutLayout = new QVBoxLayout(m_pShortcutRow);
    shortcutLayout->setContentsMargins(0, 0, 0, 0);
    shortcutLayout->setSpacing(8);
    shortcutLayout->addWidget(strong_label(tr("Jump here with a shortcut"), m_pShortcutRow));
    auto* shortcutButtons = new QHBoxLayout();
    shortcutButtons->setSpacing(8);
    m_pShortcut = new KeySequenceWidget(m_pShortcutRow);
    m_pShortcut->setKeyPrefix(QString());
    m_pShortcut->setKeyPostfix(QString());
    m_pShortcut->setMousePrefix(tr("Mouse button "));
    m_pShortcut->setMousePostfix(QString());
    m_pShortcut->setToolTip(tr("Click, then press the keys"));
    m_pShortcut->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_pShortcut, &KeySequenceWidget::keySequenceChanged, this, [this]() { shortcutChanged(); });
    shortcutButtons->addWidget(m_pShortcut, 1);
    m_pButtonClearShortcut = new QPushButton(QStringLiteral("\u2715"), m_pShortcutRow);
    m_pButtonClearShortcut->setFixedWidth(40);
    m_pButtonClearShortcut->setToolTip(tr("Clear the shortcut"));
    m_pButtonClearShortcut->setAccessibleName(tr("Clear the shortcut"));
    connect(m_pButtonClearShortcut, &QPushButton::clicked, this, [this]() { clearShortcut(); });
    shortcutButtons->addWidget(m_pButtonClearShortcut);
    shortcutLayout->addLayout(shortcutButtons);
    detailsLayout->addWidget(m_pShortcutRow);
    detailsLayout->addSpacing(8);

    auto* more = new QPushButton(tr("More settings..."), details);
    connect(more, &QPushButton::clicked, this, [this]() { editComputer(m_pDesk->selected()); });
    detailsLayout->addWidget(more);
    m_pButtonRemove = new QPushButton(tr("Remove from layout"), details);
    connect(m_pButtonRemove, &QPushButton::clicked, this, [this]() { removeComputer(m_pDesk->selected()); });
    detailsLayout->addWidget(m_pButtonRemove);
    detailsLayout->addStretch();
    detailsLayout->addWidget(muted_label(tr("Tip: if the mouse won't cross, check that Scroll Lock "
                                            "is off."), details));
    m_pSideStack->addWidget(details);
    columns->addWidget(side);

    connect(m_pDesk, &DeskView::selectionChanged, this, [this]() { updateSidePanel(); });
    connect(m_pDesk, &DeskView::aboutToChange, this, [this]() { snapshot(); });
    connect(m_pDesk, &DeskView::changed, this, [this]() { updateSidePanel(); });
    connect(m_pDesk, &DeskView::editRequested, this, [this](int index) { editComputer(index); });
    connect(m_pDesk, &DeskView::removeRequested, this, [this](int index) { removeComputer(index); });
    connect(m_pDesk, &DeskView::dropped, this, [this](const QString& name, int index) {
        placeComputer(name, index);
    });

    m_pDesk->setSelected(serverIndex());
    updateWaiting();
    updateSidePanel();
}

void ServerConfigDialog::snapshot()
{
    m_Undo.push_back(Snapshot{serverConfig().screens(), serverConfig().hotkeys(), m_Waiting,
                             m_pDesk->selected()});
    m_pButtonUndo->setEnabled(true);
}

void ServerConfigDialog::undo()
{
    if (m_Undo.empty()) {
        return;
    }
    Snapshot last = m_Undo.back();
    m_Undo.pop_back();
    serverConfig().screens() = last.screens;
    serverConfig().hotkeys() = last.hotkeys;
    m_Waiting = last.waiting;
    m_pButtonUndo->setEnabled(!m_Undo.empty());
    refreshHotkeyList();
    m_pDesk->refresh();
    m_pDesk->setSelected(last.selected);
    layoutChanged();
}

void ServerConfigDialog::layoutChanged()
{
    updateWaiting();
    updateSidePanel();
}

int ServerConfigDialog::serverIndex() const
{
    const auto& screens = m_ServerConfig.screens();
    for (int i = 0; i < static_cast<int>(screens.size()); ++i) {
        if (!screens[i].isNull() && screens[i].name() == m_ServerName) {
            return i;
        }
    }
    return -1;
}

int ServerConfigDialog::shortcutIndex(const QString& name) const
{
    // the shortcuts made here: one key that switches to one computer
    const auto& hotkeys = m_ServerConfig.hotkeys();
    for (int i = 0; i < static_cast<int>(hotkeys.size()); ++i) {
        const auto& actions = hotkeys[i].actions();
        if (actions.size() == 1 && actions[0].type() == Action::switchToScreen &&
            actions[0].switchScreenName() == name) {
            return i;
        }
    }
    return -1;
}

void ServerConfigDialog::placeComputer(const QString& name, int index)
{
    auto& screens = serverConfig().screens();
    if (index < 0 || index >= static_cast<int>(screens.size()) || !screens[index].isNull()) {
        return;
    }
    snapshot();
    screens[index] = Screen(name);
    m_Waiting.removeAll(name);
    m_pDesk->refresh();
    m_pDesk->setSelected(index);
    layoutChanged();
}

void ServerConfigDialog::placeNearServer(const QString& name)
{
    const auto& screens = serverConfig().screens();
    const int columns = serverConfig().numColumns();
    const int rows = serverConfig().numRows();
    auto free_at = [&](int column, int row) {
        return column >= 0 && column < columns && row >= 0 && row < rows &&
               screens[row * columns + column].isNull();
    };

    // next to this computer if there is room, then next to any computer
    std::vector<int> anchors;
    if (serverIndex() >= 0) {
        anchors.push_back(serverIndex());
    }
    for (int i = 0; i < columns * rows; ++i) {
        if (!screens[i].isNull() && i != serverIndex()) {
            anchors.push_back(i);
        }
    }
    for (int anchor : anchors) {
        for (const Side& side : kSides) {
            const int column = anchor % columns + side.dx;
            const int row = anchor / columns + side.dy;
            if (free_at(column, row)) {
                placeComputer(name, row * columns + column);
                return;
            }
        }
    }
    for (int i = 0; i < columns * rows; ++i) {
        if (screens[i].isNull()) {
            placeComputer(name, i);
            return;
        }
    }
    QMessageBox::information(this, tr("No room on the desk"),
                             tr("Remove a computer from the layout to make room for %1.").arg(name));
}

void ServerConfigDialog::addComputer()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Add a computer"),
        tr("Its name, as shown on its Home screen next to \"This computer\":"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    static const QRegularExpression valid("^[a-z0-9._-]+$", QRegularExpression::CaseInsensitiveOption);
    if (!valid.match(name).hasMatch()) {
        QMessageBox::warning(this, tr("Add a computer"),
                             tr("A computer name can only use letters, numbers, dots, dashes and "
                                "underscores."));
        return;
    }
    for (const Screen& screen : serverConfig().screens()) {
        if (!screen.isNull() && screen.name().compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, tr("Add a computer"),
                                     tr("%1 is already on the desk.").arg(screen.name()));
            return;
        }
    }
    placeNearServer(name);
}

void ServerConfigDialog::removeComputer(int index)
{
    auto& screens = serverConfig().screens();
    if (index < 0 || index >= static_cast<int>(screens.size()) || screens[index].isNull() ||
        index == serverIndex()) {
        return;
    }
    snapshot();
    const QString name = screens[index].name();
    screens[index] = Screen();
    // its shortcut would point at a computer that is not there any more
    const int shortcut = shortcutIndex(name);
    if (shortcut >= 0) {
        serverConfig().hotkeys().erase(serverConfig().hotkeys().begin() + shortcut);
        refreshHotkeyList();
    }
    if (!m_Waiting.contains(name)) {
        m_Waiting << name;
    }
    m_pDesk->refresh();
    m_pDesk->setSelected(serverIndex());
    layoutChanged();
}

void ServerConfigDialog::editComputer(int index)
{
    auto& screens = serverConfig().screens();
    if (index < 0 || index >= static_cast<int>(screens.size()) || screens[index].isNull()) {
        return;
    }
    Screen edited = screens[index];
    const QString oldName = edited.name();
    ScreenSettingsDialog dialog(this, &edited);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    snapshot();
    screens[index] = edited;
    if (edited.name() != oldName) {
        for (Hotkey& hotkey : serverConfig().hotkeys()) {
            for (int i = 0; i < static_cast<int>(hotkey.actions().size()); ++i) {
                Action action = hotkey.actions()[i];
                if (action.type() == Action::switchToScreen && action.switchScreenName() == oldName) {
                    action.setSwitchScreenName(edited.name());
                    hotkey.setAction(i, action);
                }
            }
        }
        if (oldName == m_ServerName) {
            m_ServerName = edited.name();
            m_pDesk->setServerName(m_ServerName);
        }
        refreshHotkeyList();
    }
    m_pDesk->refresh();
    layoutChanged();
}

void ServerConfigDialog::moveSelected(int target)
{
    auto& screens = serverConfig().screens();
    const int from = m_pDesk->selected();
    if (from < 0 || target < 0 || target == from || target >= static_cast<int>(screens.size()) ||
        !screens[target].isNull()) {
        return;
    }
    snapshot();
    std::swap(screens[from], screens[target]);
    m_pDesk->refresh();
    m_pDesk->setSelected(target);
    layoutChanged();
}

void ServerConfigDialog::shortcutChanged()
{
    const int index = m_pDesk->selected();
    if (index < 0) {
        return;
    }
    const QString name = serverConfig().screens()[index].name();
    const KeySequence sequence = m_pShortcut->keySequence();
    const int shortcut = shortcutIndex(name);
    if (!sequence.valid() ||
        (shortcut >= 0 && serverConfig().hotkeys()[shortcut].keySequence().toString() == sequence.toString())) {
        updateSidePanel();
        return;
    }
    snapshot();
    if (shortcut >= 0) {
        serverConfig().hotkeys()[shortcut].setKeySequence(sequence);
    } else {
        Action action;
        action.setType(Action::switchToScreen);
        action.setSwitchScreenName(name);
        Hotkey hotkey;
        hotkey.setKeySequence(sequence);
        hotkey.appendAction(action);
        serverConfig().hotkeys().push_back(hotkey);
    }
    refreshHotkeyList();
    updateSidePanel();
}

void ServerConfigDialog::clearShortcut()
{
    const int index = m_pDesk->selected();
    if (index < 0) {
        return;
    }
    const int shortcut = shortcutIndex(serverConfig().screens()[index].name());
    if (shortcut < 0) {
        return;
    }
    snapshot();
    serverConfig().hotkeys().erase(serverConfig().hotkeys().begin() + shortcut);
    refreshHotkeyList();
    updateSidePanel();
}

void ServerConfigDialog::refreshHotkeyList()
{
    ui_->m_pListHotkeys->clear();
    ui_->m_pListActions->clear();
    for (const Hotkey& hotkey : serverConfig().hotkeys()) {
        ui_->m_pListHotkeys->addItem(hotkey.text());
    }
}

void ServerConfigDialog::updateSidePanel()
{
    const auto& screens = serverConfig().screens();
    const int index = m_pDesk->selected();
    if (index < 0 || index >= static_cast<int>(screens.size()) || screens[index].isNull()) {
        m_pSideStack->setCurrentIndex(0);
        return;
    }
    m_pSideStack->setCurrentIndex(1);

    const QString name = screens[index].name();
    const bool server = index == serverIndex();
    m_pSideIcon->setPixmap(DeskView::icon(server, 22, QColor(glidekvm::theme::kBlue)));
    m_pSideName->setText(name);
    if (server) {
        m_pSideStatus->setText(tr("This computer"));
    } else if (m_Connected.contains(name)) {
        m_pSideStatus->setText(tr("Connected now"));
    } else {
        m_pSideStatus->setText(tr("Not connected right now"));
    }

    // every free place next to another computer
    const int columns = serverConfig().numColumns();
    const int rows = serverConfig().numRows();
    std::vector<int> others;
    if (!server && serverIndex() >= 0) {
        others.push_back(serverIndex());
    }
    for (int i = 0; i < columns * rows; ++i) {
        if (!screens[i].isNull() && i != index && i != serverIndex()) {
            others.push_back(i);
        }
    }
    m_pComboSide->clear();
    int current = -1;
    for (int other : others) {
        for (const Side& side : kSides) {
            const int column = other % columns + side.dx;
            const int row = other / columns + side.dy;
            if (column < 0 || column >= columns || row < 0 || row >= rows) {
                continue;
            }
            const int place = row * columns + column;
            if (place != index && !screens[place].isNull()) {
                continue;
            }
            if (place == index && current < 0) {
                current = m_pComboSide->count();
            }
            m_pComboSide->addItem(tr(side.text).arg(screens[other].name()), place);
        }
    }
    if (others.empty()) {
        m_pComboSide->addItem(tr("Place another computer first"), index);
        current = 0;
    } else if (current < 0) {
        m_pComboSide->insertItem(0, tr("Not next to any computer"), index);
        current = 0;
    }
    m_pComboSide->setCurrentIndex(current);
    m_pComboSide->setEnabled(!others.empty());
    m_pLabelUnreachable->setVisible(!others.empty() && m_pComboSide->itemText(0) ==
                                    tr("Not next to any computer"));

    const int shortcut = shortcutIndex(name);
    if (shortcut >= 0) {
        m_pShortcut->setKeySequence(serverConfig().hotkeys()[shortcut].keySequence());
    } else {
        m_pShortcut->setKeySequence(KeySequence());
        m_pShortcut->setText(tr("Click to set"));
    }
    m_pButtonClearShortcut->setEnabled(shortcut >= 0);
    m_pButtonRemove->setVisible(!server);
}

void ServerConfigDialog::updateWaiting()
{
    while (QLayoutItem* item = m_pWaitingChips->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QStringList placed;
    for (const Screen& screen : serverConfig().screens()) {
        if (!screen.isNull()) {
            placed << screen.name();
        }
    }
    int shown = 0;
    for (const QString& name : m_Waiting) {
        if (placed.contains(name)) {
            continue;
        }
        auto* chip = new ComputerChip(name, ui_->m_pTabScreens);
        connect(chip, &QPushButton::clicked, this, [this, name]() { placeNearServer(name); });
        m_pWaitingChips->addWidget(chip);
        ++shown;
    }
    m_pLabelWaitingHint->setText(shown == 0 ? tr("Computers that try to connect but aren't on "
                                                 "the desk show up here.")
                                            : tr("Drag onto the desk, or click to place."));
}

ServerConfigDialog::~ServerConfigDialog() = default;
