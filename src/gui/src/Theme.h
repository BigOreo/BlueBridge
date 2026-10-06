/*
    InputLeap -- mouse and keyboard sharing utility
    Copyright (C) InputLeap contributors

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

class QApplication;
class QWidget;

namespace inputleap {
namespace theme {

// Brand colors, see res/brand and the design canvas.
constexpr const char* kBlue = "#1F5EFF";
constexpr const char* kBlueDark = "#1846C2";
constexpr const char* kBlueTint = "#E8EFFF";
constexpr const char* kInk = "#0F1B2D";
constexpr const char* kSlate = "#5B6B82";
constexpr const char* kMist = "#F4F6F9";
constexpr const char* kLine = "#DCE3EC";
constexpr const char* kAmber = "#F2A93B";

// Loads the bundled fonts and applies the colors and styles to the whole
// application. Call once, before any window is created.
void apply(QApplication& app);

// Marks a button as the screen's main action (filled blue).
void set_primary(QWidget* button, bool primary = true);

} // namespace theme
} // namespace inputleap
