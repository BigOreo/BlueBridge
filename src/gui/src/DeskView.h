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

#pragma once

#include "Screen.h"

#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include <vector>

// The desk on the Arrange screens page: each computer is a tile on a grid,
// dragged to where it sits. Amber marks show where the mouse crosses.
class DeskView : public QWidget
{
    Q_OBJECT

public:
    static const char* const kMimeType;

    explicit DeskView(QWidget* parent = nullptr);

    void setScreens(std::vector<Screen>* screens, int columns, int rows);
    void setServerName(const QString& name) { m_ServerName = name; update(); }
    void setConnected(const QStringList& names) { m_Connected = names; update(); }

    // the pointer used for this computer and the monitor used for the others
    static QPixmap icon(bool server, int size, const QColor& color);

    int selected() const { return m_Selected; }
    void setSelected(int index);
    // call after the screens were changed from outside
    void refresh();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void selectionChanged(int index);
    // sent before the view moves a screen, so the change can be undone
    void aboutToChange();
    void changed();
    void editRequested(int index);
    void removeRequested(int index);
    // a waiting computer was dropped on an empty place
    void dropped(const QString& name, int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool occupied(int index) const;
    QRectF cellRect(int index) const;
    QRectF tileRect(int index) const { return cellRect(index).adjusted(6, 6, -6, -6); }
    int indexAt(const QPointF& pos) const;
    void updateWindow();
    void moveScreen(int from, int to);
    void paintTile(QPainter& painter, const QRectF& rect, int index, bool lifted) const;

    std::vector<Screen>* m_Screens = nullptr;
    int m_Columns = 0;
    int m_Rows = 0;
    QString m_ServerName;
    QStringList m_Connected;
    int m_Selected = -1;

    // the part of the grid on show: the computers plus one free place around them
    int m_FirstColumn = 0;
    int m_FirstRow = 0;
    int m_ShownColumns = 0;
    int m_ShownRows = 0;
    // the free places around the edge show only partly, leaving more room for the computers
    bool m_MarginLeft = false;
    bool m_MarginTop = false;
    bool m_MarginRight = false;
    bool m_MarginBottom = false;

    int m_PressIndex = -1;
    QPointF m_PressPos;
    QPointF m_DragPos;
    bool m_Dragging = false;
    int m_DropIndex = -1;
    bool m_ExternalDrag = false;
    // the focus ring shows only while the keyboard is in use
    bool m_KeyboardFocus = false;
};

// A computer waiting to be placed. Drag it onto the desk, or click it to put
// it next to this computer.
class ComputerChip : public QPushButton
{
    Q_OBJECT

public:
    ComputerChip(const QString& name, QWidget* parent);
    const QString& name() const { return m_Name; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QString m_Name;
    QPoint m_PressPos;
};
