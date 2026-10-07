/*
 * GlideKVM -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
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

#include "SetupWizard.h"
#include "ui_SetupWizard.h"
#include "MainWindow.h"
#include "QGlideKVMApplication.h"
#include "QUtility.h"
#include "Theme.h"

#include <QMessageBox>
#include <QPixmap>

SetupWizard::SetupWizard(MainWindow& mainWindow, bool startMain) :
    ui_{std::make_unique<Ui::SetupWizard>()},
    m_MainWindow(mainWindow),
    m_StartMain(startMain)
{
    ui_->setupUi(this);

#if defined(Q_OS_MAC)

    // the mac style needs a little more room because of the
    // graphic on the left.
    resize(600, 500);
    setMinimumSize(size());

#elif defined(Q_OS_WIN)

    // when aero is disabled on windows, the next/back buttons
    // are hidden (must be a qt bug) -- resizing the window
    // to +1 of the original height seems to fix this.
    // NOTE: calling setMinimumSize after this will break
    // it again, so don't do that.
    resize(size().width(), size().height() + 1);

#endif

    // the same look on every platform, with the app icon in the header
    setWizardStyle(QWizard::ModernStyle);
    setPixmap(QWizard::LogoPixmap, QPixmap(":/res/icons/256x256/glidekvm.png")
                                       .scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    QFont choiceFont = ui_->m_pServerRadioButton->font();
    choiceFont.setWeight(QFont::DemiBold);
    ui_->m_pServerRadioButton->setFont(choiceFont);
    ui_->m_pClientRadioButton->setFont(choiceFont);
    const QString secondary = QStringLiteral("color: %1;").arg(glidekvm::theme::kSlate);
    ui_->m_pLabelServerDescription->setStyleSheet(secondary);
    ui_->m_pLabelClientDescription->setStyleSheet(secondary);
    ui_->m_pLabelWelcomeNote->setStyleSheet(secondary);

    connect(ui_->m_pServerRadioButton, &QRadioButton::toggled, &m_MainWindow, &MainWindow::setServerMode);
    connect(ui_->m_pClientRadioButton, &QRadioButton::toggled, this, [=] (bool clientMode) {
        m_MainWindow.setServerMode(!clientMode);
    });

    m_Locale.fillLanguageComboBox(ui_->m_pComboLanguage);
    setIndexFromItemData(ui_->m_pComboLanguage, m_MainWindow.appConfig().language());
}

SetupWizard::~SetupWizard() = default;

bool SetupWizard::validateCurrentPage()
{
    QMessageBox message;
    message.setWindowTitle(tr("Set up GlideKVM"));
    message.setIcon(QMessageBox::Information);

    if (currentPage() == ui_->m_pNodePage)
    {
        bool result = ui_->m_pClientRadioButton->isChecked() ||
                 ui_->m_pServerRadioButton->isChecked();

        if (!result)
        {
            message.setText(tr("Choose whose keyboard and mouse this computer will use."));
            message.exec();
            return false;
        }
    }

    return true;
}

void SetupWizard::initializePage(int id)
{
    QWizard::initializePage(id);
    if (page(id) != ui_->m_pDonePage) {
        return;
    }

    QString steps;
    const bool server = ui_->m_pServerRadioButton->isChecked();
    ui_->m_pDonePage->setSubTitle(server ? tr("One thing left, on your other computer.")
                                         : tr("Two quick checks before you connect."));
    if (server) {
        steps = tr("<ol style=\"margin-left: 0px; -qt-list-indent: 1;\">"
                   "<li style=\"margin-bottom: 8px;\">Install GlideKVM on the other computer and choose "
                   "<span style=\"font-weight: 600;\">Another computer's</span>.</li>"
                   "<li style=\"margin-bottom: 8px;\">Connecting over Bluetooth? Pair the two computers first in "
                   "your Bluetooth settings.</li>"
                   "<li style=\"margin-bottom: 8px;\">Back here, click <span style=\"font-weight: 600;\">Configure Server</span> and place the other "
                   "computer where it sits on your desk.</li></ol>");
    } else {
        steps = tr("<ol style=\"margin-left: 0px; -qt-list-indent: 1;\">"
                   "<li style=\"margin-bottom: 8px;\">Make sure GlideKVM is running on your main computer, with "
                   "<span style=\"font-weight: 600;\">This computer's</span> chosen.</li>"
                   "<li style=\"margin-bottom: 8px;\">Connecting over Bluetooth? Pair the two computers first in "
                   "your Bluetooth settings.</li>"
                   "<li style=\"margin-bottom: 8px;\">In the next window, pick your main computer and click "
                   "<span style=\"font-weight: 600;\">Start</span>.</li></ol>");
    }
    ui_->m_pLabelNextSteps->setText(steps);
}

void SetupWizard::changeEvent(QEvent* event)
{
    if (event != nullptr)
    {
        switch (event->type())
        {
        case QEvent::LanguageChange:
            {
                ui_->m_pComboLanguage->blockSignals(true);
                ui_->retranslateUi(this);
                ui_->m_pComboLanguage->blockSignals(false);
                break;
            }

        default:
            QWizard::changeEvent(event);
        }
    }
}

void SetupWizard::accept()
{
    AppConfig& appConfig = m_MainWindow.appConfig();

    appConfig.setLanguage(ui_->m_pComboLanguage->itemData(ui_->m_pComboLanguage->currentIndex()).toString());

    appConfig.setAutoStart(ui_->m_pCheckBoxAutoStart->isChecked());
    appConfig.setWizardHasRun();
    appConfig.saveSettings();

    QSettings& settings = m_MainWindow.settings();
    if (ui_->m_pServerRadioButton->isChecked())
    {
        settings.setValue("groupServerChecked", true);
        settings.setValue("groupClientChecked", false);
    }
    if (ui_->m_pClientRadioButton->isChecked())
    {
        settings.setValue("groupClientChecked", true);
        settings.setValue("groupServerChecked", false);
    }

    QWizard::accept();

    if (m_StartMain)
    {
        m_MainWindow.updateZeroconfService();
        m_MainWindow.open();
    }
}

void SetupWizard::reject()
{
    QGlideKVMApplication::getInstance()->switchTranslator(m_MainWindow.appConfig().language());

    if (m_StartMain)
    {
        m_MainWindow.open();
    }

    QWizard::reject();
}

void SetupWizard::on_m_pComboLanguage_currentIndexChanged(int index)
{
    QString ietfCode = ui_->m_pComboLanguage->itemData(index).toString();
    QGlideKVMApplication::getInstance()->switchTranslator(ietfCode);
}
