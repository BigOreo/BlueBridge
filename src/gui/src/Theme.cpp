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

#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

namespace glidekvm {
namespace theme {

namespace {

void load_fonts()
{
    const QStringList fonts = {
        ":/res/fonts/IBMPlexSans-Regular.ttf",
        ":/res/fonts/IBMPlexSans-Medium.ttf",
        ":/res/fonts/IBMPlexSans-SemiBold.ttf",
        ":/res/fonts/Sora-SemiBold.ttf",
        ":/res/fonts/Sora-Bold.ttf",
    };
    for (const auto& font : fonts) {
        QFontDatabase::addApplicationFont(font);
    }
}

QPalette brand_palette()
{
    QPalette palette;
    const QColor ink(kInk);
    const QColor muted("#9AA8BB");

    palette.setColor(QPalette::Window, QColor(kMist));
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(kMist));
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::Button, Qt::white);
    palette.setColor(QPalette::ButtonText, ink);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(kBlue));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, QColor(kBlue));
    palette.setColor(QPalette::LinkVisited, QColor(kBlueDark));
    palette.setColor(QPalette::ToolTipBase, ink);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(kSlate));
    palette.setColor(QPalette::Mid, QColor("#C9D3E0"));
    palette.setColor(QPalette::Midlight, QColor(kMist));
    palette.setColor(QPalette::Light, Qt::white);
    palette.setColor(QPalette::Dark, QColor(kSlate));

    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, muted);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(kLine));
    return palette;
}

// Only the widgets whose look carries the brand are styled; combo boxes,
// spin boxes and other controls keep the Fusion style tinted by the palette.
QString brand_style_sheet()
{
    return QStringLiteral(R"(
QGroupBox {
    background: #FFFFFF;
    border: 1px solid #DCE3EC;
    border-radius: 12px;
    margin-top: 18px;
    padding: 16px 12px 10px 12px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 4px;
    color: #0F1B2D;
    font-weight: 600;
}
QPushButton {
    background: #FFFFFF;
    color: #0F1B2D;
    border: 1px solid #C9D3E0;
    border-radius: 8px;
    padding: 6px 16px;
    min-height: 22px;
}
QPushButton:hover { background: #F4F6F9; }
QPushButton:pressed { background: #E8EFFF; }
QPushButton:focus { border-color: #1F5EFF; }
QPushButton:disabled { color: #9AA8BB; border-color: #E3E8EF; background: #FFFFFF; }
QPushButton[primary="true"] {
    background: #1F5EFF;
    border-color: #1F5EFF;
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton[primary="true"]:hover { background: #1846C2; border-color: #1846C2; }
QPushButton[primary="true"]:pressed { background: #1846C2; }
QPushButton[primary="true"]:focus { border: 2px solid #0F1B2D; }
QPushButton[primary="true"]:disabled { background: #C9D3E0; border-color: #C9D3E0; color: #FFFFFF; }
QLineEdit {
    background: #FFFFFF;
    border: 1px solid #C9D3E0;
    border-radius: 8px;
    padding: 5px 8px;
    selection-background-color: #1F5EFF;
}
QLineEdit:focus { border-color: #1F5EFF; }
QLineEdit:disabled { background: #F4F6F9; color: #9AA8BB; }
QListWidget#serverList { background: transparent; border: 0; outline: 0; }
QListWidget#serverList::item {
    background: #FFFFFF;
    border: 1px solid #C9D3E0;
    border-radius: 10px;
    padding: 8px 10px;
    margin: 3px 0;
    color: #0F1B2D;
}
QListWidget#serverList::item:selected {
    background: #E8EFFF;
    border: 2px solid #1F5EFF;
    color: #0F1B2D;
}
QListWidget#serverList::item:hover:!selected { background: #F4F6F9; }
QWidget#homeContent, QScrollArea { background: #F4F6F9; }
QWidget#sidebar { background: #FFFFFF; border-right: 1px solid #DCE3EC; }
QLabel[role="wordmark"] { font-family: "Sora"; font-weight: 700; font-size: 18px; color: #0F1B2D; }
QPushButton[nav="true"] {
    text-align: left;
    background: transparent;
    border: 0;
    border-radius: 10px;
    padding: 9px 12px;
    color: #3A4A61;
    min-height: 22px;
}
QPushButton[nav="true"]:hover { background: #F4F6F9; }
QPushButton[nav="true"]:checked { background: #E8EFFF; color: #0F1B2D; font-weight: 600; }
QFrame#roleSwitch { background: #E3E8EF; border-radius: 12px; }
QPushButton[segment="true"] {
    background: transparent;
    border: 0;
    border-radius: 9px;
    padding: 8px 16px;
    color: #3A4A61;
    min-height: 22px;
}
QPushButton[segment="true"]:checked { background: #FFFFFF; color: #0F1B2D; font-weight: 600; }
QPushButton[segment="true"]:disabled { color: #9AA8BB; }
QFrame[card="true"] { background: #FFFFFF; border: 1px solid #DCE3EC; border-radius: 16px; }
QFrame[tile="true"] { background: #F4F6F9; border: 0; border-radius: 12px; }
QLabel[role="cardTitle"] { font-family: "Sora"; font-weight: 600; font-size: 16px; color: #0F1B2D; }
QLabel[role="pageTitle"] { font-family: "Sora"; font-weight: 700; font-size: 20px; color: #0F1B2D; }
QLabel[role="muted"] { color: #5B6B82; }
QLabel[role="strong"] { font-weight: 600; color: #0F1B2D; }
QLabel[role="address"] { font-size: 16px; font-weight: 500; color: #0F1B2D; }
QLabel[role="stepNumber"] {
    background: #E8EFFF;
    color: #1846C2;
    border-radius: 15px;
    font-weight: 600;
}
QFrame#hero { background: #0B1424; border: 0; border-radius: 18px; }
QFrame#hero QLabel { background: transparent; color: #FFFFFF; }
QLabel[role="heroTitle"] { font-family: "Sora"; font-weight: 700; font-size: 22px; }
QFrame#hero QLabel[role="heroText"] { color: #C3CEDD; }
QFrame#hero QPushButton[primary="false"] {
    background: transparent;
    color: #FFFFFF;
    border: 1px solid #3A4A61;
}
QFrame#hero QPushButton[primary="false"]:hover { background: #16223A; }
QPushButton#m_pButtonToggleStart { padding: 8px 22px; font-size: 14px; }
QListWidget#clientList { background: transparent; border: 0; outline: 0; }
QListWidget#clientList::item {
    background: #F4F6F9;
    border: 0;
    border-radius: 10px;
    padding: 8px 10px;
    margin: 2px 0;
    color: #0F1B2D;
}
QToolTip {
    background: #0F1B2D;
    color: #FFFFFF;
    border: 0;
    padding: 6px 8px;
}
)");
}

} // namespace

void apply(QApplication& app)
{
    load_fonts();

    // Fusion draws the same on every platform and follows the palette, so
    // the brand colors look the same on Windows, macOS and Linux.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }
    app.setPalette(brand_palette());

    QFont font(QStringLiteral("IBM Plex Sans"));
    font.setPointSizeF(app.font().pointSizeF() > 0 ? app.font().pointSizeF() : 10.0);
    app.setFont(font);

    app.setStyleSheet(brand_style_sheet());
}

void set_primary(QWidget* button, bool primary)
{
    if (button == nullptr) {
        return;
    }
    button->setProperty("primary", primary);
    // re-evaluate the style sheet for the changed property
    button->style()->unpolish(button);
    button->style()->polish(button);
}

} // namespace theme
} // namespace glidekvm
