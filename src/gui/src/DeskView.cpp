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

#include "DeskView.h"
#include "Theme.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>
#include <cmath>

const char* const DeskView::kMimeType = "application/x-glidekvm-computer";

namespace {

const QColor kDot(0xC9, 0xD3, 0xE0);
const QColor kSlot(0x9A, 0xA8, 0xBB);
const QColor kServerSubtitle(0xDC, 0xE6, 0xFF);
// tiles are drawn in the proportions of a wide screen
constexpr qreal kAspect = 1.6;
constexpr qreal kMaxCellWidth = 190;
// how much of a free place around the edge is hidden
constexpr qreal kMarginCut = 0.6;

QPointF event_pos(const QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

template <typename Event>
QPointF drop_pos(const Event* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return QPointF(event->pos());
#endif
}

// the pointer from the sidebar's Home icon, in a 24 px box
void draw_cursor(QPainter& painter, const QPointF& at, qreal size, const QColor& color)
{
    const qreal k = size / 24.0;
    QPainterPath path;
    path.moveTo(at + QPointF(5, 3) * k);
    path.lineTo(at + QPointF(19, 10) * k);
    path.lineTo(at + QPointF(13, 12) * k);
    path.lineTo(at + QPointF(11, 18) * k);
    path.closeSubpath();
    painter.setPen(QPen(color, 1.75 * k, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void draw_monitor(QPainter& painter, const QPointF& at, qreal size, const QColor& color)
{
    const qreal k = size / 24.0;
    painter.setPen(QPen(color, 1.75 * k, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(at + QPointF(4, 4) * k, QSizeF(16, 11) * k), 1.5 * k, 1.5 * k);
    painter.drawLine(at + QPointF(9, 20) * k, at + QPointF(15, 20) * k);
    painter.drawLine(at + QPointF(12, 15) * k, at + QPointF(12, 20) * k);
}

} // namespace

QPixmap DeskView::icon(bool server, int size, const QColor& color)
{
    // drawn at twice the size so it stays sharp on high density displays
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    if (server) {
        draw_cursor(painter, QPointF(0, 0), size * 2, color);
    } else {
        draw_monitor(painter, QPointF(0, 0), size * 2, color);
    }
    painter.end();
    pixmap.setDevicePixelRatio(2);
    return pixmap;
}

DeskView::DeskView(QWidget* parent) :
    QWidget(parent)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAccessibleName(tr("Desk layout"));
    setToolTip(tr("Drag a computer to move it. Double-click it for more settings."));
}

void DeskView::setScreens(std::vector<Screen>* screens, int columns, int rows)
{
    m_Screens = screens;
    m_Columns = columns;
    m_Rows = rows;
    refresh();
}

void DeskView::setSelected(int index)
{
    if (!occupied(index)) {
        index = -1;
    }
    if (index == m_Selected) {
        return;
    }
    m_Selected = index;
    update();
    emit selectionChanged(index);
}

void DeskView::refresh()
{
    if (!occupied(m_Selected)) {
        m_Selected = -1;
    }
    updateWindow();
    update();
}

QSize DeskView::sizeHint() const
{
    return QSize(520, 300);
}

QSize DeskView::minimumSizeHint() const
{
    return QSize(300, 200);
}

bool DeskView::occupied(int index) const
{
    return m_Screens != nullptr && index >= 0 && index < m_Columns * m_Rows &&
           index < static_cast<int>(m_Screens->size()) && !(*m_Screens)[index].isNull();
}

void DeskView::updateWindow()
{
    int left = m_Columns;
    int top = m_Rows;
    int right = -1;
    int bottom = -1;
    for (int i = 0; i < m_Columns * m_Rows; ++i) {
        if (occupied(i)) {
            left = std::min(left, i % m_Columns);
            right = std::max(right, i % m_Columns);
            top = std::min(top, i / m_Columns);
            bottom = std::max(bottom, i / m_Columns);
        }
    }
    if (right < 0) {
        m_FirstColumn = 0;
        m_FirstRow = 0;
        m_ShownColumns = m_Columns;
        m_ShownRows = m_Rows;
        m_MarginLeft = m_MarginTop = m_MarginRight = m_MarginBottom = false;
        return;
    }
    // one free place on every side, where there is room for it
    m_MarginLeft = left > 0;
    m_MarginTop = top > 0;
    m_MarginRight = right < m_Columns - 1;
    m_MarginBottom = bottom < m_Rows - 1;
    left = std::max(0, left - 1);
    top = std::max(0, top - 1);
    right = std::min(m_Columns - 1, right + 1);
    bottom = std::min(m_Rows - 1, bottom + 1);
    m_FirstColumn = left;
    m_FirstRow = top;
    m_ShownColumns = right - left + 1;
    m_ShownRows = bottom - top + 1;
}

QRectF DeskView::cellRect(int index) const
{
    if (m_ShownColumns <= 0 || m_ShownRows <= 0) {
        return QRectF();
    }
    const QRectF area = QRectF(rect()).adjusted(16, 16, -16, -16);
    const qreal columns = m_ShownColumns - kMarginCut * (int(m_MarginLeft) + int(m_MarginRight));
    const qreal rows = m_ShownRows - kMarginCut * (int(m_MarginTop) + int(m_MarginBottom));
    qreal width = std::min(area.width() / columns, area.height() / rows * kAspect);
    width = std::min(width, kMaxCellWidth);
    const qreal height = width / kAspect;
    const QPointF origin(area.center().x() - width * columns / 2 - (m_MarginLeft ? kMarginCut * width : 0),
                         area.center().y() - height * rows / 2 - (m_MarginTop ? kMarginCut * height : 0));
    const int column = index % m_Columns - m_FirstColumn;
    const int row = index / m_Columns - m_FirstRow;
    return QRectF(origin.x() + column * width, origin.y() + row * height, width, height);
}

int DeskView::indexAt(const QPointF& pos) const
{
    for (int row = m_FirstRow; row < m_FirstRow + m_ShownRows; ++row) {
        for (int column = m_FirstColumn; column < m_FirstColumn + m_ShownColumns; ++column) {
            const int index = row * m_Columns + column;
            if (cellRect(index).contains(pos)) {
                return index;
            }
        }
    }
    return -1;
}

void DeskView::moveScreen(int from, int to)
{
    if (from == to || !occupied(from) || to < 0 || to >= m_Columns * m_Rows) {
        return;
    }
    emit aboutToChange();
    // a computer dropped on another one swaps places with it
    std::swap((*m_Screens)[from], (*m_Screens)[to]);
    m_Selected = to;
    updateWindow();
    update();
    emit selectionChanged(to);
    emit changed();
}

void DeskView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // the desk
    QPainterPath desk;
    desk.addRoundedRect(QRectF(rect()), 12, 12);
    painter.fillPath(desk, QColor(glidekvm::theme::kMist));
    painter.setPen(QPen(kDot, 2, Qt::SolidLine, Qt::RoundCap));
    for (int x = 10; x < width(); x += 20) {
        for (int y = 10; y < height(); y += 20) {
            painter.drawPoint(x, y);
        }
    }

    if (m_Screens == nullptr) {
        return;
    }

    // free places, while something is being dragged
    const bool dragging = m_Dragging || m_ExternalDrag;
    if (dragging) {
        for (int row = m_FirstRow; row < m_FirstRow + m_ShownRows; ++row) {
            for (int column = m_FirstColumn; column < m_FirstColumn + m_ShownColumns; ++column) {
                const int index = row * m_Columns + column;
                if (occupied(index) && !(m_Dragging && index == m_PressIndex)) {
                    continue;
                }
                const bool target = index == m_DropIndex;
                QPen pen(target ? QColor(glidekvm::theme::kBlue) : kSlot, target ? 2 : 1.2,
                         Qt::DashLine);
                painter.setPen(pen);
                painter.setBrush(target ? QColor(glidekvm::theme::kBlueTint) : QColor(Qt::transparent));
                painter.drawRoundedRect(tileRect(index), 8, 8);
            }
        }
    }

    // where the mouse crosses: wherever two computers touch
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(glidekvm::theme::kAmber));
    for (int i = 0; i < m_Columns * m_Rows; ++i) {
        if (!occupied(i) || (m_Dragging && i == m_PressIndex)) {
            continue;
        }
        const QRectF cell = cellRect(i);
        const int right = i + 1;
        if (i % m_Columns + 1 < m_Columns && occupied(right) && !(m_Dragging && right == m_PressIndex)) {
            const qreal length = cell.height() * 0.5;
            painter.drawRoundedRect(QRectF(cell.right() - 3, cell.center().y() - length / 2, 6, length), 3, 3);
        }
        const int below = i + m_Columns;
        if (below < m_Columns * m_Rows && occupied(below) && !(m_Dragging && below == m_PressIndex)) {
            const qreal length = cell.width() * 0.4;
            painter.drawRoundedRect(QRectF(cell.center().x() - length / 2, cell.bottom() - 3, length, 6), 3, 3);
        }
    }

    for (int i = 0; i < m_Columns * m_Rows; ++i) {
        if (occupied(i) && !(m_Dragging && i == m_PressIndex)) {
            paintTile(painter, tileRect(i), i, false);
        }
    }

    if (m_Dragging) {
        QRectF lifted = tileRect(m_PressIndex);
        lifted.moveCenter(m_DragPos);
        paintTile(painter, lifted, m_PressIndex, true);
    }

    if (hasFocus() && m_KeyboardFocus && occupied(m_Selected) && !m_Dragging) {
        painter.setPen(QPen(QColor(glidekvm::theme::kBlue), 2, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(tileRect(m_Selected).adjusted(-4, -4, 4, 4), 11, 11);
    }
}

void DeskView::paintTile(QPainter& painter, const QRectF& rect, int index, bool lifted) const
{
    const Screen& screen = (*m_Screens)[index];
    const bool server = screen.name() == m_ServerName;
    const bool selected = index == m_Selected;
    const QColor ink(glidekvm::theme::kInk);

    if (selected || lifted) {
        painter.setPen(Qt::NoPen);
        for (int k = 1; k <= 4; ++k) {
            painter.setBrush(QColor(15, 27, 45, 12));
            painter.drawRoundedRect(rect.adjusted(-k, -k + 4, k, k + 4), 8 + k, 8 + k);
        }
    }

    const qreal border = selected || lifted ? 3 : 2;
    painter.setPen(QPen(server && !selected ? QColor(glidekvm::theme::kBlue) : ink, border));
    painter.setBrush(server ? QColor(glidekvm::theme::kBlue) : QColor(Qt::white));
    painter.drawRoundedRect(rect.adjusted(border / 2, border / 2, -border / 2, -border / 2), 8, 8);

    const QColor text = server ? QColor(Qt::white) : ink;
    const QColor subtle = server ? kServerSubtitle : QColor(glidekvm::theme::kSlate);

    QString subtitle;
    if (server) {
        const int displays = QGuiApplication::screens().size();
        subtitle = displays > 1 ? tr("This computer · %1 displays").arg(displays) : tr("This computer");
    } else {
        subtitle = m_Connected.contains(screen.name()) ? tr("Connected") : tr("Not connected");
    }

    QFont nameFont = font();
    nameFont.setWeight(QFont::DemiBold);
    nameFont.setPixelSize(rect.height() > 70 ? 13 : 12);
    QFont smallFont = font();
    smallFont.setPixelSize(11);
    const QFontMetricsF nameMetrics(nameFont);
    const QFontMetricsF smallMetrics(smallFont);

    const bool roomForIcon = rect.height() >= 72;
    const qreal iconSize = roomForIcon ? 18 : 0;
    const qreal gap = 4;
    const qreal total = iconSize + (roomForIcon ? gap : 0) + nameMetrics.height() + 2 + smallMetrics.height();
    qreal y = rect.center().y() - total / 2;

    if (roomForIcon) {
        const QPointF at(rect.center().x() - iconSize / 2, y);
        if (server) {
            draw_cursor(painter, at, iconSize, text);
        } else {
            draw_monitor(painter, at, iconSize, QColor(glidekvm::theme::kBlue));
        }
        y += iconSize + gap;
    }

    const qreal textWidth = rect.width() - 16;
    painter.setPen(text);
    painter.setFont(nameFont);
    painter.drawText(QRectF(rect.left() + 8, y, textWidth, nameMetrics.height()), Qt::AlignCenter,
                     nameMetrics.elidedText(screen.name(), Qt::ElideRight, textWidth));
    y += nameMetrics.height() + 2;
    painter.setPen(subtle);
    painter.setFont(smallFont);
    painter.drawText(QRectF(rect.left() + 8, y, textWidth, smallMetrics.height()), Qt::AlignCenter,
                     smallMetrics.elidedText(subtitle, Qt::ElideRight, textWidth));
}

void DeskView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_KeyboardFocus = false;
    const int index = indexAt(event_pos(event));
    if (occupied(index)) {
        setSelected(index);
        m_PressIndex = index;
        m_PressPos = event_pos(event);
    }
}

void DeskView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event_pos(event);
    if (!(event->buttons() & Qt::LeftButton) || m_PressIndex < 0) {
        setCursor(occupied(indexAt(pos)) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    if (!m_Dragging && (pos - m_PressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_Dragging = true;
        setCursor(Qt::ClosedHandCursor);
    }
    if (m_Dragging) {
        m_DragPos = pos;
        m_DropIndex = indexAt(pos);
        update();
    }
}

void DeskView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const bool dragged = m_Dragging;
    const int from = m_PressIndex;
    const int to = m_DropIndex;
    m_Dragging = false;
    m_PressIndex = -1;
    m_DropIndex = -1;
    setCursor(Qt::OpenHandCursor);
    if (dragged && to >= 0) {
        moveScreen(from, to);
    }
    update();
}

void DeskView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int index = indexAt(event_pos(event));
    if (event->button() == Qt::LeftButton && occupied(index)) {
        emit editRequested(index);
    }
}

void DeskView::focusInEvent(QFocusEvent* event)
{
    m_KeyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    QWidget::focusInEvent(event);
}

void DeskView::keyPressEvent(QKeyEvent* event)
{
    m_KeyboardFocus = true;
    update();
    int dx = 0;
    int dy = 0;
    switch (event->key()) {
    case Qt::Key_Left: dx = -1; break;
    case Qt::Key_Right: dx = 1; break;
    case Qt::Key_Up: dy = -1; break;
    case Qt::Key_Down: dy = 1; break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (occupied(m_Selected)) {
            emit removeRequested(m_Selected);
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (occupied(m_Selected)) {
            emit editRequested(m_Selected);
        }
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    if (!occupied(m_Selected)) {
        for (int i = 0; i < m_Columns * m_Rows; ++i) {
            if (occupied(i)) {
                setSelected(i);
                break;
            }
        }
        return;
    }

    const int column = m_Selected % m_Columns;
    const int row = m_Selected / m_Columns;
    if (event->modifiers() & Qt::ControlModifier) {
        // Ctrl and an arrow moves the computer
        const int c = column + dx;
        const int r = row + dy;
        if (c >= 0 && c < m_Columns && r >= 0 && r < m_Rows) {
            moveScreen(m_Selected, r * m_Columns + c);
        }
        return;
    }
    // an arrow selects the next computer that way
    for (int c = column + dx, r = row + dy; c >= 0 && c < m_Columns && r >= 0 && r < m_Rows;
         c += dx, r += dy) {
        if (occupied(r * m_Columns + c)) {
            setSelected(r * m_Columns + c);
            return;
        }
    }
}

void DeskView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(kMimeType)) {
        m_ExternalDrag = true;
        event->acceptProposedAction();
        update();
    } else {
        event->ignore();
    }
}

void DeskView::dragMoveEvent(QDragMoveEvent* event)
{
    const int index = indexAt(drop_pos(event));
    if (index >= 0 && !occupied(index)) {
        m_DropIndex = index;
        event->acceptProposedAction();
    } else {
        m_DropIndex = -1;
        event->ignore();
    }
    update();
}

void DeskView::dragLeaveEvent(QDragLeaveEvent*)
{
    m_ExternalDrag = false;
    m_DropIndex = -1;
    update();
}

void DeskView::dropEvent(QDropEvent* event)
{
    const int index = indexAt(drop_pos(event));
    const QString name = QString::fromUtf8(event->mimeData()->data(kMimeType));
    m_ExternalDrag = false;
    m_DropIndex = -1;
    update();
    if (index >= 0 && !occupied(index) && !name.isEmpty()) {
        event->acceptProposedAction();
        emit dropped(name, index);
    }
}

ComputerChip::ComputerChip(const QString& name, QWidget* parent) :
    QPushButton(parent),
    m_Name(name)
{
    setText(name);
    setProperty("chip", true);
    setIcon(QIcon(DeskView::icon(false, 16, QColor(glidekvm::theme::kInk))));
    setIconSize(QSize(16, 16));
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Drag onto the desk, or click to put it next to this computer."));
}

void ComputerChip::mousePressEvent(QMouseEvent* event)
{
    m_PressPos = event_pos(event).toPoint();
    QPushButton::mousePressEvent(event);
}

void ComputerChip::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) ||
        (event_pos(event).toPoint() - m_PressPos).manhattanLength() < QApplication::startDragDistance()) {
        QPushButton::mouseMoveEvent(event);
        return;
    }
    setDown(false);
    auto* mime = new QMimeData();
    mime->setData(DeskView::kMimeType, m_Name.toUtf8());
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const QPixmap pixmap = grab();
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    drag->exec(Qt::MoveAction);
}
