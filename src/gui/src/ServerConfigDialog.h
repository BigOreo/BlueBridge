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

#pragma once

#include "DeskView.h"
#include "ServerConfig.h"

#include <QDialog>
#include <QStringList>
#include <memory>
#include <vector>

class DeskView;
class KeySequenceWidget;
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace Ui
{
    class ServerConfigDialog;
}

class ServerConfigDialog : public QDialog
{
    Q_OBJECT

    public:
        ServerConfigDialog(QWidget* parent, ServerConfig& config, const QString& defaultScreenName);
        ~ServerConfigDialog() override;

    public slots:
        void accept() override;
        void message(const QString& message) { m_Message = message; }

    public:
        // computers connected now, and ones that tried to connect but are not
        // in the layout yet
        void setComputers(const QStringList& connected, const QStringList& waiting);

    protected slots:
        void on_m_pButtonNewHotkey_clicked();
        void on_m_pListHotkeys_itemSelectionChanged();
        void on_m_pButtonEditHotkey_clicked();
        void on_m_pButtonRemoveHotkey_clicked();

        void on_m_pButtonNewAction_clicked();
        void on_m_pListActions_itemSelectionChanged();
        void on_m_pButtonEditAction_clicked();
        void on_m_pButtonRemoveAction_clicked();
        void on_m_pCheckBoxEnableClipboard_stateChanged(int state);

    protected:
        ServerConfig& serverConfig() { return m_ServerConfig; }
        void setOrigServerConfig(const ServerConfig& s) { m_OrigServerConfig = s; }

    private:
        struct Snapshot {
            std::vector<Screen> screens;
            std::vector<Hotkey> hotkeys;
            QStringList waiting;
            int selected;
        };

        void buildLayoutTab();
        void snapshot();
        void undo();
        int serverIndex() const;
        int shortcutIndex(const QString& name) const;
        void placeComputer(const QString& name, int index);
        void placeNearServer(const QString& name);
        void addComputer();
        void removeComputer(int index);
        void editComputer(int index);
        void moveSelected(int target);
        void shortcutChanged();
        void clearShortcut();
        void refreshHotkeyList();
        void updateSidePanel();
        DeviceKind kindOf(const QString& name) const;
        void updateWaiting();
        void layoutChanged();

    private:
        std::unique_ptr<Ui::ServerConfigDialog> ui_;
        ServerConfig& m_OrigServerConfig;
        ServerConfig m_ServerConfig;
        QString m_Message;
        QString m_ServerName;
        QStringList m_Connected;
        QStringList m_Waiting;
        std::vector<Snapshot> m_Undo;

        DeskView* m_pDesk = nullptr;
        QPushButton* m_pButtonUndo = nullptr;
        QCheckBox* m_pCheckNeedsControl = nullptr;
        QHBoxLayout* m_pWaitingChips = nullptr;
        QLabel* m_pLabelWaitingHint = nullptr;
        QStackedWidget* m_pSideStack = nullptr;
        QLabel* m_pSideIcon = nullptr;
        QLabel* m_pSideName = nullptr;
        QLabel* m_pSideStatus = nullptr;
        QComboBox* m_pComboSide = nullptr;
        QComboBox* m_pComboKind = nullptr;
        QLabel* m_pLabelUnreachable = nullptr;
        QWidget* m_pShortcutRow = nullptr;
        KeySequenceWidget* m_pShortcut = nullptr;
        QPushButton* m_pButtonClearShortcut = nullptr;
        QPushButton* m_pButtonRemove = nullptr;
};
