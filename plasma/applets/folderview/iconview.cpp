/*
 *   Copyright © 2008 Fredrik Höglund <fredrik@kde.org>
 *   Copyright © 2008 Rafael Fernández López <ereslibre@kde.org>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Library General Public
 *   License as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public License
 *   along with this library; see the file COPYING.LIB.  If not, write to
 *   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *   Boston, MA 02110-1301, USA.
 */

#include "iconview.h"

#include <QApplication>
#include <QDebug>
#include <QDrag>
#include <QGraphicsView>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsProxyWidget>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPaintEngine>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>

#include <KDirModel>
#include <KFileItemDelegate>
#include <KGlobalSettings>

#include <konqmimedata.h>
#include <konq_operations.h>

#include "dirlister.h"
#include "proxymodel.h"
#include "previewpluginsmodel.h"

#include "plasma/containment.h"
#include "plasma/corona.h"
#include "plasma/paintutils.h"
#include "plasma/theme.h"
#include "plasma/widgets/scrollbar.h"

#ifdef Q_WS_X11
#  include <QX11Info>
#  include <X11/Xlib.h>

#  undef FontChange
#endif


IconView::IconView(QGraphicsItem *parent)
    : QGraphicsWidget(parent),
      m_lastScrollValue(0),
      m_columns(0),
      m_rows(0),
      m_validRows(0),
      m_layoutBroken(false),
      m_needPostLayoutPass(false),
      m_initialListing(true),
      m_positionsLoaded(false),
      m_viewScrolled(false),
      m_doubleClick(false),
      m_dragInProgress(false),
      m_iconsLocked(false),
      m_alignToGrid(false),
      m_wordWrap(false),
      m_drawShadows(true),
      m_flow(QListView::LeftToRight)
{
    setAcceptHoverEvents(true);
    setAcceptDrops(true);
    setCacheMode(NoCache);

    m_scrollBar = new Plasma::ScrollBar(Qt::Vertical, this);
    m_scrollBar->hide();
    connect(m_scrollBar->nativeWidget(), SIGNAL(valueChanged(int)), SLOT(scrollBarValueChanged(int)));

    int size = style()->pixelMetric(QStyle::PM_LargeIconSize);
    m_iconSize = QSize(size, size);
    m_gridSize = QSize(size * 2, size * 2);
}

IconView::~IconView()
{
}

void IconView::setModel(QAbstractItemModel *model)
{
    m_model = static_cast<ProxyModel*>(model);
    m_dirModel = static_cast<KDirModel*>(m_model->sourceModel());

    connect(m_model, SIGNAL(rowsInserted(QModelIndex,int,int)), SLOT(rowsInserted(QModelIndex,int,int)));
    connect(m_model, SIGNAL(rowsRemoved(QModelIndex,int,int)), SLOT(rowsRemoved(QModelIndex,int,int)));
    connect(m_model, SIGNAL(modelReset()), SLOT(modelReset()));
    connect(m_model, SIGNAL(layoutChanged()), SLOT(layoutChanged()));
    connect(m_model, SIGNAL(dataChanged(QModelIndex,QModelIndex)), SLOT(dataChanged(QModelIndex,QModelIndex)));

    KDirLister *lister = m_dirModel->dirLister();
    connect(lister, SIGNAL(started(KUrl)), SLOT(listingStarted(KUrl)));
    connect(lister, SIGNAL(clear()), SLOT(listingClear()));
    connect(lister, SIGNAL(completed()), SLOT(listingCompleted()));
    connect(lister, SIGNAL(canceled()), SLOT(listingCanceled()));
    connect(lister, SIGNAL(showErrorMessage(QString)), SLOT(listingError(QString)));

    m_validRows = 0;
    m_layoutBroken = false;

    if (m_model->rowCount() > 0) {
        m_delayedLayoutTimer.start(10, this);
        emit busy(true);
    }
}

QAbstractItemModel *IconView::model() const
{
    return m_model;
}

void IconView::setSelectionModel(QItemSelectionModel *model)
{
    m_selectionModel = model;
}

QItemSelectionModel *IconView::selectionModel() const
{
    return m_selectionModel;
}

void IconView::setItemDelegate(KFileItemDelegate *delegate)
{
    m_delegate = static_cast<KFileItemDelegate*>(delegate);

    connect(m_delegate, SIGNAL(closeEditor(QWidget*,QAbstractItemDelegate::EndEditHint)),
            SLOT(closeEditor(QWidget*,QAbstractItemDelegate::EndEditHint)));
    connect(m_delegate, SIGNAL(commitData(QWidget*)), SLOT(commitData(QWidget*)));
}

KFileItemDelegate *IconView::itemDelegate() const
{
    return m_delegate;
}

void IconView::setIconSize(const QSize &size)
{
    if (size != m_iconSize) {
        m_iconSize = size;

        // Schedule a full relayout
        if (m_validRows > 0) {
            m_validRows = 0;
            m_delayedLayoutTimer.start(10, this);
        }
    }
}

QSize IconView::iconSize() const
{
    return m_iconSize;
}

void IconView::setGridSize(const QSize &size)
{
    if (size != m_gridSize) {
        m_gridSize = size;

        // Schedule a full relayout
        if (m_validRows > 0) {
            m_validRows = 0;
            m_delayedLayoutTimer.start(10, this);
        }
    }
}

QSize IconView::gridSize() const
{
    return m_gridSize;
}

void IconView::setWordWrap(bool on)
{
    if (m_wordWrap != on) {
        m_wordWrap = on;

        // Schedule a full relayout
        if (m_validRows > 0) {
            m_validRows = 0;
            m_delayedLayoutTimer.start(10, this);
        }
    }
}

bool IconView::wordWrap() const
{
    return m_wordWrap;
}

void IconView::setFlow(QListView::Flow flow)
{
    if (m_flow != flow) {
        m_flow = flow;

        if (!m_layoutBroken && m_validRows > 0) {
            m_validRows = 0;
            m_delayedLayoutTimer.start(10, this);
        }
    }
}

QListView::Flow IconView::flow() const
{
    return m_flow;
}

void IconView::setAlignToGrid(bool on)
{
    if (on && !m_alignToGrid && m_validRows > 0) {
        alignIconsToGrid();
    }

    m_alignToGrid = on;
}

bool IconView::alignToGrid() const
{
    return m_alignToGrid;
}

void IconView::setIconsMoveable(bool on)
{
    m_iconsLocked = !on;
}

bool IconView::iconsMoveable() const
{
    return !m_iconsLocked;
}

void IconView::setDrawShadows(bool on)
{
    if (m_drawShadows != on) {
        m_drawShadows = on;
        markAreaDirty(visibleArea());
        update();
    }
}

bool IconView::drawShadows() const
{
    return m_drawShadows;
}

void IconView::setIconPositionsData(const QStringList &data)
{
    // Sanity checks
    if (data.size() < 5 || data.at(0).toInt() != 1 || ((data.size() - 2) % 3) ||
        data.at(1).toInt() != ((data.size() - 2) / 3)) {
        return;
    }

    const QPoint offset = contentsRect().topLeft().toPoint();
    for (int i = 2; i < data.size(); i += 3) {
        const QString &name = data.at(i);
        int x = data.at(i + 1).toInt();
        int y = data.at(i + 2).toInt();
        m_savedPositions.insert(name, QPoint(x, y) + offset);
    }
}

QStringList IconView::iconPositionsData() const
{
    QStringList data;

    if (m_layoutBroken && !m_initialListing && m_validRows == m_items.size()) {
        int version = 1;
        data << QString::number(version);
        data << QString::number(m_items.size());

        const QPoint offset = contentsRect().topLeft().toPoint();
        const QSize size = gridSize();
        for (int i = 0; i < m_items.size(); i++) {
            QModelIndex index = m_model->index(i, 0);
            KFileItem item = m_model->itemForIndex(index);
            data << item.name();
            data << QString::number(m_items[i].rect.x() - offset.x());
            data << QString::number(m_items[i].rect.y() - offset.y());
        }
    }

    return data;
}

QScrollBar *IconView::verticalScrollBar() const
{
    return m_scrollBar->nativeWidget();
}

QRect IconView::visibleArea() const
{
    return mapToViewport(contentsRect()).toAlignedRect();
}

void IconView::rowsInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(parent)
    m_regionCache.clear();

    if (!m_layoutBroken || m_initialListing) {
        if (first < m_validRows) {
            m_validRows = 0;
        }
        m_delayedLayoutTimer.start(10, this);
        emit busy(true);
    } else {
        const QStyleOptionViewItemV4 option = viewOptions();
        const QRect cr = contentsRect().toRect();
        const QSize grid = gridSize();
        QPoint pos = QPoint();

        m_items.insert(first, last - first + 1, ViewItem());

        // If a single item was inserted and we have a saved position from a deleted file,
        // reuse that position.
        if (first == last && !m_lastDeletedPos.isNull()) {
            const QModelIndex index = m_model->index(first, 0);
            QSize size = m_delegate->sizeHint(option, index).boundedTo(grid);
            size.rwidth() = grid.width();
            m_items[first].rect = QRect(m_lastDeletedPos, size);
            m_items[first].layouted = true;
            markAreaDirty(m_items[first].rect);
            m_lastDeletedPos = QPoint();
            m_validRows = m_items.size();
            return;
        }

        // Lay out the newly inserted files
        for (int i = first; i <= last; i++) {
            const QModelIndex index = m_model->index(i, 0);
            const QSize size = m_delegate->sizeHint(option, index).boundedTo(grid);
            pos = findNextEmptyPosition(pos, grid, cr);
            m_items[i].rect = QRect(pos.x() + (grid.width() - size.width()) / 2, pos.y(),
                                    size.width(), size.height());
            m_items[first].layouted = true;
            markAreaDirty(m_items[i].rect);
        }

        m_validRows = m_items.size();
        updateScrollBar();
    }
}

void IconView::rowsRemoved(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(parent)

    m_regionCache.clear();

    if (!m_layoutBroken) {
        if (first < m_validRows) {
            m_validRows = 0;
        }
        m_delayedLayoutTimer.start(10, this);
        emit busy(true);
    } else {
        for (int i = first; i <= last; i++) {
            markAreaDirty(m_items[i].rect);
        }
        // When a single item is removed, we'll save the position and use it for the next new item.
        // The reason for this is that when a file is renamed, it will first be removed from the view
        // and then reinserted.
        if (first == last) {
            const QSize size = gridSize();
            m_lastDeletedPos.rx() = m_items[first].rect.x() - (size.width() - m_items[first].rect.width()) / 2;
            m_lastDeletedPos.ry() = m_items[first].rect.y();
        }
        m_items.remove(first, last - first + 1);
        m_validRows = m_items.size();
    }

    // check the folder to see if it exists!!!
    emit checkFolder();
}

void IconView::modelReset()
{
    m_savedPositions.clear();
    m_layoutBroken = false;
    m_validRows = 0;

    m_delayedLayoutTimer.start(10, this);
    emit busy(true);
}

void IconView::layoutChanged()
{
    m_savedPositions.clear();
    m_layoutBroken = false;
    m_validRows = 0;

    m_delayedLayoutTimer.start(10, this);
    emit busy(true);
}

void IconView::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    const QStyleOptionViewItemV4 option = viewOptions();
    const QSize grid = gridSize();
    m_regionCache.clear();

    // Update the size of the items and center them in the grid cell
    for (int i = topLeft.row(); i <= bottomRight.row(); i++) {
        if (!m_items[i].layouted) {
            continue;
        }
        const QModelIndex index = m_model->index(i, 0);
        QSize size = m_delegate->sizeHint(option, index).boundedTo(grid);
        size.rwidth() = grid.width();
        QRect dirty = m_items[i].rect;
        if (size != m_items[i].rect.size()) {
            m_items[i].rect.setHeight(size.height());
            dirty |= m_items[i].rect;
        }
        markAreaDirty(dirty);
    }
}

void IconView::listingStarted(const KUrl &url)
{
    Q_UNUSED(url)

    // Reset any error message that may have resulted from an earlier listing
    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        update();
    }

    emit busy(true);

    // check the folder to see if it exists!!!
    emit checkFolder();
}

void IconView::listingClear()
{
    m_initialListing = true;
    markAreaDirty(visibleArea());
    update();
}

void IconView::listingCompleted()
{
    m_delayedCacheClearTimer.start(5000, this);
    m_initialListing = false;

    if (m_validRows == m_model->rowCount()) {
        emit busy(false);
    }
}

void IconView::listingCanceled()
{
    m_delayedCacheClearTimer.start(5000, this);
    m_initialListing = false;

    if (m_validRows == m_model->rowCount()) {
        emit busy(false);
    }
}

void IconView::listingError(const QString &message)
{
    m_errorMessage = message;
    markAreaDirty(visibleArea());
    update();

    if (m_validRows == m_model->rowCount()) {
        emit busy(false);
    }
}

void IconView::scrollBarValueChanged(int value)
{
    Q_UNUSED(value)

    m_viewScrolled = true;
    update();
}

int IconView::columnsForWidth(qreal width) const
{
    int spacing = 10;
    int margin = 10;

    qreal available = width - 2 * margin;
    return qFloor(available / (gridSize().width() + spacing));
}

int IconView::rowsForHeight(qreal height) const
{
    int spacing = 10;
    int margin = 10;

    qreal available = height - 2 * margin;
    return qFloor(available / (gridSize().height() + spacing));
}

QPointF IconView::mapToViewport(const QPointF &point) const
{
    return point + QPointF(0, m_scrollBar->value());
}

QRectF IconView::mapToViewport(const QRectF &rect) const
{
    return rect.translated(0, m_scrollBar->value());
}

QPointF IconView::mapFromViewport(const QPointF &point) const
{
    return point - QPointF(0, m_scrollBar->value());
}

QRectF IconView::mapFromViewport(const QRectF &rect) const
{
    return rect.translated(0, -m_scrollBar->value());
}

QPoint inline IconView::nextGridPosition(const QPoint &lastPos, const QSize &grid, const QRect &contentRect) const
{
    int spacing = 10;
    int margin = 10;

    if (lastPos.isNull()) {
        return QPoint(contentRect.left() + margin, contentRect.top() + margin);
    }

    QPoint pos = lastPos;

    if (m_flow == QListView::LeftToRight) {
        pos.rx() += grid.width() + spacing;
        if ((pos.x() + grid.width() + 10) >= (contentRect.right() - m_scrollBar->geometry().width() - margin)) {
            pos.ry() += grid.height() + spacing;
            pos.rx() = contentRect.left() + margin;
        }
    } else {
        pos.ry() += grid.height() + spacing;
        if ((pos.y() + grid.height() + 10) >= (contentRect.bottom() - margin)) {
            pos.rx() += grid.width() + spacing;
            pos.ry() = contentRect.top() + margin;
        }
    }

    return pos;
}

QPoint IconView::findNextEmptyPosition(const QPoint &prevPos, const QSize &gridSize, const QRect &contentRect) const
{
    QPoint pos = prevPos;
    bool done = false;

    while (!done)
    {
        done = true;
        pos = nextGridPosition(pos, gridSize, contentRect);
        const QRect r(pos, gridSize);
        for (int i = 0; i < m_items.count(); i++) {
            if (m_items.at(i).rect.intersects(r)) {
                done = false;
                break;
            }
        }
    }

    return pos;
}

void IconView::layoutItems()
{
    QStyleOptionViewItemV4 option = viewOptions();
    m_items.resize(m_model->rowCount());
    m_regionCache.clear();

    const QRect visibleRect = mapToViewport(contentsRect()).toAlignedRect();
    const QRect rect = contentsRect().toRect();
    const QSize grid = gridSize();
    int maxWidth = rect.width() - m_scrollBar->geometry().width();
    int maxHeight = rect.height();
    m_columns = columnsForWidth(maxWidth);
    m_rows = rowsForHeight(maxHeight);
    bool needUpdate = false;

    m_delegate->setMaximumSize(grid);

    // If we're starting with the first item
    if (m_validRows == 0) {
        m_needPostLayoutPass = false;
        m_currentLayoutPos = QPoint();
    }

    if (!m_savedPositions.isEmpty()) {
        m_layoutBroken = true;
        // Restart the delayed cache clear timer if it's running and we haven't
        // finished laying out the icons.
        if (m_delayedCacheClearTimer.isActive() && m_validRows < m_items.size()) {
             m_delayedCacheClearTimer.start(5000, this);
        }
    } else {
        m_layoutBroken = false;
    }

    // Do a 20 millisecond layout pass
    QTime time;
    time.start();
    do {
        const int count = qMin(m_validRows + 50, m_items.size());
        if (!m_savedPositions.isEmpty()) {

            // Layout with saved icon positions
            // ================================================================
            for (int i = m_validRows; i < count; i++) {
                const QModelIndex index = m_model->index(i, 0);
                KFileItem item = m_model->itemForIndex(index);
                QSize size = m_delegate->sizeHint(option, index).boundedTo(grid);
                size.rwidth() = grid.width();

                const QPoint pos = m_savedPositions.value(item.name(), QPoint(-1, -1));
                if (pos != QPoint(-1, -1)) {
                    m_items[i].rect = QRect(pos, size);
                    m_items[i].layouted = true;
                    if (m_items[i].rect.intersects(visibleRect)) {
                        needUpdate = true;
                    }
                } else {
                    // We don't have a saved position for this file, so we'll record the
                    // size and lay it out in a second layout pass.
                    m_items[i].rect = QRect(QPoint(), size);
                    m_items[i].layouted = false;
                    m_needPostLayoutPass = true;
                }
            }
            // If we've finished laying out all the icons
            if (!m_initialListing && !m_needPostLayoutPass && count == m_items.size()) {
                needUpdate |= doLayoutSanityCheck();
            }
        } else {

            // Automatic layout
            // ================================================================
            QPoint pos = m_currentLayoutPos;
            for (int i = m_validRows; i < count; i++) {
                const QModelIndex index = m_model->index(i, 0);
                QSize size = m_delegate->sizeHint(option, index).boundedTo(grid);
                size.rwidth() = grid.width();

                pos = nextGridPosition(pos, grid, rect);
                m_items[i].rect = QRect(pos, size);
                m_items[i].layouted = true;
                if (m_items[i].rect.intersects(visibleRect)) {
                    needUpdate = true;
                }
            }
            m_currentLayoutPos = pos;
        }
        m_validRows = count;
    } while (m_validRows < m_items.size() && time.elapsed() < 30);


    // Second layout pass for files that didn't have a saved position
    // ====================================================================
    if (m_validRows == m_items.size() && m_needPostLayoutPass) {
        QPoint pos = QPoint();
        for (int i = 0; i < m_items.size(); i++) {
            if (m_items[i].layouted) {
                continue;
            }
            pos = findNextEmptyPosition(pos, grid, rect);
            m_items[i].rect.moveTo(pos.x() + (grid.width() - m_items[i].rect.width()) / 2, pos.y());
            if (m_items[i].rect.intersects(visibleRect)) {
                needUpdate = true;
            }
        }
        needUpdate |= doLayoutSanityCheck();
        m_needPostLayoutPass = false;
        return;
    }

    if (m_validRows < m_items.size() || m_needPostLayoutPass) {
        m_delayedLayoutTimer.start(10, this);
    } else if (!m_initialListing) {
        emit busy(false);
    }

    if (needUpdate) {
        m_dirtyRegion = QRegion(visibleRect);
        update();
    }

    updateScrollBar();
}

void IconView::alignIconsToGrid()
{
    int margin = 10;
    int spacing = 10;
    const QRect cr = contentsRect().toRect();
    const QSize size = gridSize() + QSize(spacing, spacing);
    int topMargin = margin + cr.top();
    int leftMargin = margin + cr.left();
    int vOffset = topMargin + size.height() / 2;
    int hOffset = leftMargin + size.width() / 2;
    bool layoutChanged = false;

    for (int i = 0; i < m_items.size(); i++) {
        const QPoint center = m_items[i].rect.center();
        const int col = qRound((center.x() - hOffset) / qreal(size.width()));
        const int row = qRound((center.y() - vOffset) / qreal(size.height()));

        const QPoint pos(leftMargin + col * size.width() + (size.width() - m_items[i].rect.width() - spacing) / 2,
                         topMargin + row * size.height());

        if (pos != m_items[i].rect.topLeft()) {
            m_items[i].rect.moveTo(pos);
            layoutChanged = true;
        }
    }

    if (layoutChanged) {
        doLayoutSanityCheck();
        updateScrollBar();
        markAreaDirty(visibleArea());
        m_layoutBroken = true;
        m_savedPositions.clear();
    }
}

bool IconView::doLayoutSanityCheck()
{
    // Make sure that the distance from the top of the viewport to the
    // topmost item is 10 pixels.
    int minY = INT_MAX;
    for (int i = 0; i < m_validRows; i++) {
        if (!m_items[i].layouted) {
            continue;
        }
        minY = qMin(minY, m_items[i].rect.y());
    }

    int topMargin = contentsRect().top() + 10;
    if (minY != topMargin) {
        int delta = topMargin - minY;
        for (int i = 0; i < m_validRows; i++) {
            if (!m_items[i].layouted) {
                continue;
            }
            m_items[i].rect.translate(0, delta);
        }
        return true;
    }

    return false;
}

void IconView::paintErrorMessage(QPainter *painter, const QRect &rect, const QString &message) const
{
    QIcon icon = KIconLoader::global()->loadIcon("dialog-error", KIconLoader::NoGroup, KIconLoader::SizeHuge,
                                                 KIconLoader::DefaultState, QStringList(), 0, true);
    const QSize iconSize = icon.isNull() ? QSize() :
                               icon.actualSize(QSize(KIconLoader::SizeHuge, KIconLoader::SizeHuge));
    const int flags = Qt::AlignCenter | Qt::TextWordWrap;
    const int blur = qCeil(m_delegate->shadowBlur());

    QFontMetrics fm = painter->fontMetrics();
    QRect r = fm.boundingRect(rect.adjusted(0, 0, -iconSize.width() - 4, 0), flags, message);
    QPixmap pm(r.size());
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setFont(painter->font());
    p.setPen(palette().color(QPalette::Text));
    p.drawText(pm.rect(), flags, message);
    p.end();

    QImage shadow;
    if (m_delegate->shadowColor().alpha() > 0) {
        shadow = QImage(pm.size() + QSize(blur * 2, blur * 2), QImage::Format_ARGB32_Premultiplied);
        p.begin(&shadow);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(shadow.rect(), Qt::transparent);
        p.drawPixmap(blur, blur, pm);
        p.end();

        Plasma::PaintUtils::shadowBlur(shadow, blur, m_delegate->shadowColor());
    }

    const QSize size(pm.width() + iconSize.width() + 4, qMax(iconSize.height(), pm.height()));
    const QPoint iconPos = rect.topLeft() + QPoint((rect.width() - size.width()) / 2,
                                                   (rect.height() - size.height()) / 2);
    const QPoint textPos = iconPos + QPoint(iconSize.width() + 4, (iconSize.height() - pm.height()) / 2);

    if (!icon.isNull()) {
        icon.paint(painter, QRect(iconPos, iconSize));
    }

    if (!shadow.isNull()) {
        painter->drawImage(textPos - QPoint(blur, blur) + m_delegate->shadowOffset().toPoint(), shadow);
    }
    painter->drawPixmap(textPos, pm);
}

void IconView::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget)

    const QRect cr = contentsRect().toRect();
    if (!cr.isValid()) {
        return;
    }

    QRect clipRect = cr & option->exposedRect.toAlignedRect();
    if (clipRect.isEmpty()) {
        return;
    }

    // Make sure the backbuffer pixmap has the same size as the content rect
    if (m_pixmap.size() != cr.size()) {
        QPixmap pixmap(cr.size());
        pixmap.fill(Qt::transparent);
        if (!m_pixmap.isNull()) {
            // Static content optimization
#ifdef Q_WS_X11
            if (m_pixmap.paintEngine()->type() == QPaintEngine::X11) {
                GC gc = XCreateGC(QX11Info::display(), pixmap.handle(), 0, NULL);
                XCopyArea(QX11Info::display(), m_pixmap.handle(), pixmap.handle(), gc, 0, 0,
                          m_pixmap.width(), m_pixmap.height(), 0, 0);
                XFreeGC(QX11Info::display(), gc);
            } else
#endif
            {
                QPainter p(&pixmap);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.drawPixmap(0, 0, m_pixmap);
            }
            QRegion region(pixmap.rect());
            region -= m_pixmap.rect();
            region.translate(0, m_scrollBar->value());
            m_dirtyRegion |= region;
        } else {
            m_dirtyRegion = QRegion(visibleArea());
        }
        m_pixmap = pixmap;
    }

    if (m_viewScrolled) {
        m_dirtyRegion += scrollBackbufferContents();
        m_viewScrolled = false;
    }

    int offset = m_scrollBar->value();

    painter->setClipRect(clipRect);


    // Update the dirty region in the backbuffer
    // =========================================
    if (!m_dirtyRegion.isEmpty()) {
        QStyleOptionViewItemV4 opt = viewOptions();

        QPainter p(&m_pixmap);
        p.translate(-cr.topLeft() - QPoint(0, offset));
        p.setClipRegion(m_dirtyRegion);

        // Clear the dirty region
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(mapToViewport(cr).toAlignedRect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        for (int i = 0; i < m_validRows; i++) {
            opt.rect = m_items[i].rect;

            if (!m_items[i].layouted || !m_dirtyRegion.intersects(opt.rect)) {
                continue;
            }

            const QModelIndex index = m_model->index(i, 0);
            opt.state &= ~(QStyle::State_HasFocus | QStyle::State_MouseOver | QStyle::State_Selected);

            if (index == m_hoveredIndex) {
                opt.state |= QStyle::State_MouseOver;
            }

            if (m_selectionModel->isSelected(index)) {
                if (m_dragInProgress) {
                    continue;
                }
                updateTextShadows(palette().color(QPalette::HighlightedText));
                opt.state |= QStyle::State_Selected;
            } else {
                updateTextShadows(palette().color(QPalette::Text));
            }

            if (hasFocus() && index == m_selectionModel->currentIndex()) {
                opt.state |= QStyle::State_HasFocus;
            }

            m_delegate->paint(&p, opt, index);
        }

        if (m_rubberBand.isValid())
        {
            QStyleOptionRubberBand opt;
            initStyleOption(&opt);
            opt.rect   = m_rubberBand;
            opt.shape  = QRubberBand::Rectangle;
            opt.opaque = false;

            style()->drawControl(QStyle::CE_RubberBand, &opt, &p);
        }

        m_dirtyRegion = QRegion();
    }

    const QRect topFadeRect(cr.x(), cr.y(), cr.width(), 16);
    const QRect bottomFadeRect(cr.bottomLeft() - QPoint(0, 16), QSize(cr.width(), 16));

    // Draw the backbuffer on the Applet
    // =================================
    if ((offset > 0 && topFadeRect.intersects(clipRect)) ||
        (m_viewportRect.height() > (offset + cr.height()) && bottomFadeRect.intersects(clipRect)))
    {
        QPixmap pixmap = m_pixmap;
        QPainter p(&pixmap);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);

        // Fade out the top section of the pixmap if the scrollbar slider isn't at the top
        if (offset > 0 && topFadeRect.intersects(clipRect))
        {
            if (m_topFadeTile.isNull())
            {
                m_topFadeTile = QPixmap(256, 16);
                m_topFadeTile.fill(Qt::transparent);
                QLinearGradient g(0, 0, 0, 16);
                g.setColorAt(0, Qt::transparent);
                g.setColorAt(1, Qt::black);
                QPainter p(&m_topFadeTile);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(0, 0, 256, 16, g);
                p.end();
            }
            p.drawTiledPixmap(0, 0, m_pixmap.width(), 16, m_topFadeTile);
        }

        // Fade out the bottom part of the pixmap if the scrollbar slider isn't at the bottom
        if (m_viewportRect.height() > (offset + cr.height()) && bottomFadeRect.intersects(clipRect))
        {
            if (m_topFadeTile.isNull())
            {
                m_bottomFadeTile = QPixmap(256, 16);
                m_bottomFadeTile.fill(Qt::transparent);
                QLinearGradient g(0, 0, 0, 16);
                g.setColorAt(0, Qt::black);
                g.setColorAt(1, Qt::transparent);
                QPainter p(&m_bottomFadeTile);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(0, 0, 256, 16, g);
                p.end();
            }
            p.drawTiledPixmap(0, m_pixmap.height() - 16, m_pixmap.width(), 16, m_bottomFadeTile);
        }
        p.end();

        painter->drawPixmap(cr.topLeft(), pixmap);
    }
    else
    {
        painter->drawPixmap(cr.topLeft(), m_pixmap);
    }
    if (!m_errorMessage.isEmpty()) {
        paintErrorMessage(painter, cr, m_errorMessage);
    }
}

void IconView::updateScrollBar()
{
    // Find the height of the viewport
    int maxY = 0;
    for (int i = 0; i < m_items.size(); i++) {
        maxY = qMax(maxY, m_items[i].rect.bottom());
    }

    m_viewportRect = contentsRect();
    m_viewportRect.setBottom(qMax<int>(m_viewportRect.bottom(), maxY + 10));
    m_viewportRect.setWidth(m_viewportRect.width() - m_scrollBar->geometry().width());

    int max = int(m_viewportRect.height() - contentsRect().height());

    // Keep the scrollbar handle at the bottom if it was at the bottom and the viewport
    // has grown vertically
    bool updateValue = (m_scrollBar->minimum() != m_scrollBar->maximum()) &&
            (max > m_scrollBar->maximum()) && (m_scrollBar->value() == m_scrollBar->maximum());

    m_scrollBar->setRange(0, max);
    m_scrollBar->setPageStep(contentsRect().height());
    m_scrollBar->setSingleStep(10);

    if (updateValue) {
        m_scrollBar->setValue(max);
    }

    if (max > 0) {
        m_scrollBar->show();
    } else {
        m_scrollBar->hide();
    }
}

// Marks the supplied rect, in viewport coordinates, as dirty and schedules a repaint.
void IconView::markAreaDirty(const QRect &rect)
{
    if (rect.isEmpty()) {
        return;
    }

    const QRect visibleRect = mapToViewport(contentsRect()).toAlignedRect();
    if (!rect.intersects(visibleRect)) {
        return;
    }

    m_dirtyRegion += rect;
    update(mapFromViewport(rect));
}

// This function scrolls the contents of the backbuffer the distance the scrollbar
// has moved since the last time this function was called.
QRect IconView::scrollBackbufferContents()
{
    int value =  m_scrollBar->value();
    int delta = m_lastScrollValue - value;
    m_lastScrollValue = value;

    if (qAbs(delta) >= m_pixmap.height()) {
        return mapToViewport(contentsRect()).toAlignedRect();
    }

    int sy, dy, h;
    QRect dirty;
    if (delta < 0) {
        dy = 0;
        sy = -delta;
        h = m_pixmap.height() - sy;
        dirty = QRect(0, m_pixmap.height() - sy, m_pixmap.width(), sy);
    } else {
        dy = delta;
        sy = 0;
        h = m_pixmap.height() - dy;
        dirty = QRect(0, 0, m_pixmap.width(), dy);
    }
#ifdef Q_WS_X11
    // Avoid the overhead of creating a QPainter to do the blit.
    Display *dpy = QX11Info::display();
    GC gc = XCreateGC(dpy, m_pixmap.handle(), 0, 0);
    XCopyArea(dpy, m_pixmap.handle(), m_pixmap.handle(), gc, 0, sy, m_pixmap.width(), h, 0, dy);
    XFreeGC(dpy, gc);
#else
    m_pixmap = m_pixmap.copy(0, sy, m_pixmap.width(), h);
#endif
    return mapToViewport(dirty.translated(contentsRect().topLeft().toPoint())).toAlignedRect();
}

void IconView::updateTextShadows(const QColor &textColor)
{
    if (!m_drawShadows) {
        m_delegate->setShadowColor(Qt::transparent);
        return;
    }

    QColor shadowColor;

    // Use black shadows with bright text, and white shadows with dark text.
    if (qGray(textColor.rgb()) > 192) {
        shadowColor = Qt::black;
    } else {
        shadowColor = Qt::white;
    }

    if (m_delegate->shadowColor() != shadowColor)
    {
        m_delegate->setShadowColor(shadowColor);

        // Center white shadows to create a halo effect, and offset dark shadows slightly.
        if (shadowColor == Qt::white) {
            m_delegate->setShadowOffset(QPoint(0, 0));
        } else {
            m_delegate->setShadowOffset(QPoint(layoutDirection() == Qt::RightToLeft ? -1 : 1, 1));
        }
    }
}

bool IconView::indexIntersectsRect(const QModelIndex &index, const QRect &rect) const
{
    QRect r = m_items[index.row()].rect;
    if (!r.intersects(rect)) {
        return false;
    }

    // If the item is fully contained in the rect
    if (r.left() > rect.left() && r.right() < rect.right() &&
        r.top() > rect.top() && r.bottom() < rect.bottom())
    {
        return true;
    }

    // If the item is partially inside the rect
    return visualRegion(index).intersects(rect);
}

QModelIndex IconView::indexAt(const QPointF &point) const
{
    if (!mapToViewport(contentsRect()).contains(point))
        return QModelIndex();

    const QPoint pt = point.toPoint();

    // If we have a hovered index, check it before walking the list
    if (m_hoveredIndex.isValid()) {
        if (m_items[m_hoveredIndex.row()].rect.contains(pt) &&
            visualRegion(m_hoveredIndex).contains(pt))
        {
            return m_hoveredIndex;
        }
    }

    for (int i = 0; i < m_validRows; i++) {
        if (!m_items[i].layouted || !m_items[i].rect.contains(pt)) {
            continue;
        }

        const QModelIndex index = m_model->index(i, 0);
        if (visualRegion(index).contains(pt)) {
            return index;
        }
        break;
    }

    return QModelIndex();
}

QRect IconView::visualRect(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_validRows ||
        !m_items[index.row()].layouted) {
        return QRect();
    }

    return m_items[index.row()].rect;
}

QRegion IconView::visualRegion(const QModelIndex &index) const
{
    QStyleOptionViewItemV4 option = viewOptions();
    option.rect = m_items[index.row()].rect;
    if (m_selectionModel->isSelected(index)) {
        option.state |= QStyle::State_Selected;
    }
    if (index == m_hoveredIndex) {
        option.state |= QStyle::State_MouseOver;
    }

    quint64 key = quint64(option.state) << 32 | index.row();
    if (QRegion *region = m_regionCache.object(key)) {
        return *region;
    }

    QRegion region;
    // Make this a virtual function in KDE 5
    QMetaObject::invokeMethod(m_delegate, "shape", Q_RETURN_ARG(QRegion, region),
                              Q_ARG(QStyleOptionViewItem, option),
                              Q_ARG(QModelIndex, index));

    m_regionCache.insert(key, new QRegion(region));
    return region;
}

void IconView::updateScrollBarGeometry()
{
    QRectF cr = contentsRect();

    QRectF r = QRectF(cr.right() - m_scrollBar->geometry().width(), cr.top(),
                      m_scrollBar->geometry().width(), cr.height());
    if (m_scrollBar->geometry() != r) {
        m_scrollBar->setGeometry(r);
    }
}

void IconView::renameSelectedIcon()
{
    QModelIndex index = m_selectionModel->currentIndex();
    if (!index.isValid())
        return;

    // Don't allow renaming of files the aren't visible in the view
    const QRect rect = visualRect(index);
    if (!mapToViewport(contentsRect()).contains(rect)) {
        return;
    }

    QStyleOptionViewItemV4 option = viewOptions();
    option.rect = rect;

    QWidget *editor = m_delegate->createEditor(0, option, index);
    editor->setAttribute(Qt::WA_NoSystemBackground);
    editor->installEventFilter(m_delegate);

    QGraphicsProxyWidget *proxy = new QGraphicsProxyWidget(this);
    proxy->setWidget(editor);

    m_delegate->updateEditorGeometry(editor, option, index);
    m_delegate->setEditorData(editor, index);

    editor->show();
    editor->setFocus();

    m_editorIndex = index;
}

void IconView::commitData(QWidget *editor)
{
    m_delegate->setModelData(editor, m_model, m_editorIndex);
}

void IconView::closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint)
{
    Q_UNUSED(hint)

    if (editor->hasFocus()) {
        setFocus();
    }
    editor->hide();
    editor->removeEventFilter(m_delegate);
    editor->deleteLater();

    markAreaDirty(visibleArea());
}

void IconView::resizeEvent(QGraphicsSceneResizeEvent *)
{
    updateScrollBarGeometry();

    int maxWidth  = contentsRect().width() - m_scrollBar->geometry().width();
    int maxHeight = contentsRect().height();

    if (m_validRows > 0)
    {
        if ((m_flow == QListView::LeftToRight && columnsForWidth(maxWidth) != m_columns) ||
            (m_flow == QListView::TopToBottom && rowsForHeight(maxHeight) != m_rows))
        {
            // The scrollbar range will be updated after the re-layout
            m_validRows = 0;
            m_delayedLayoutTimer.start(10, this);
        } else {
            updateScrollBar();
            markAreaDirty(visibleArea());
        }
    }
}

void IconView::focusInEvent(QFocusEvent *event)
{
    Q_UNUSED(event)
    markAreaDirty(visibleArea());
}

void IconView::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event)
    markAreaDirty(visibleArea());
}

void IconView::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    const QModelIndex index = indexAt(mapToViewport(event->pos()));
    if (index.isValid()) {
        m_hoveredIndex = index;
        markAreaDirty(visualRect(index));
    }
}

void IconView::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event)

    if (m_hoveredIndex.isValid()) {
        markAreaDirty(visualRect(m_hoveredIndex));
        m_hoveredIndex = QModelIndex();
    }
}

void IconView::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    const QModelIndex index = indexAt(mapToViewport(event->pos()));
    if (index != m_hoveredIndex) {
        markAreaDirty(visualRect(index));
        markAreaDirty(visualRect(m_hoveredIndex));
        m_hoveredIndex = index;
    }
}

void IconView::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!contentsRect().contains(event->pos()) ||
        !m_errorMessage.isEmpty())
    {
        event->ignore();
        return;
    }

    const QPointF pos = mapToViewport(event->pos());
    setFocus(Qt::MouseFocusReason);

    if (event->button() == Qt::RightButton) {
        const QModelIndex index = indexAt(pos);
        if (index.isValid()) {
            if (!m_selectionModel->isSelected(index)) {
                m_selectionModel->select(index, QItemSelectionModel::ClearAndSelect);
                m_selectionModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
                markAreaDirty(visibleArea());
            }
            event->ignore(); // Causes contextMenuEvent() to get called
        } else if (m_selectionModel->hasSelection()) {
            m_selectionModel->clearSelection();
            markAreaDirty(visibleArea());
        }
        return;
    }

    if (event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(pos);

        // If an icon was pressed
        if (index.isValid())
        {
            if (event->modifiers() & Qt::ControlModifier) {
                m_selectionModel->select(index, QItemSelectionModel::Toggle);
                m_selectionModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
                markAreaDirty(visualRect(index));
            } else if (!m_selectionModel->isSelected(index)) {
                m_selectionModel->select(index, QItemSelectionModel::ClearAndSelect);
                m_selectionModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
                markAreaDirty(visibleArea());
            }
            m_pressedIndex = index;
            m_buttonDownPos = pos;
            return;
        }

        // If empty space was pressed
        m_pressedIndex = QModelIndex();
        m_buttonDownPos = pos;

        if (event->modifiers() & Qt::ControlModifier) {
            // Make the current selection persistent
            m_selectionModel->select(m_selectionModel->selection(), QItemSelectionModel::Select);
        } else if (static_cast<Plasma::Containment*>(parentWidget())->isContainment() &&
                   event->widget()->window()->inherits("DashboardView"))
        {
            // Let the event propagate to the parent widget, which will emit releaseVisualFocus().
            // We prefer hiding the Dashboard to allowing rubber band selections in the containment
            // when the icon view is being shown on the Dashboard.
            event->ignore();
            return;
        }

        if (m_selectionModel->hasSelection()) {
            if (!(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
                m_selectionModel->clearSelection();
                markAreaDirty(visibleArea());
            }
        }
    }
}

void IconView::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (m_rubberBand.isValid()) {
            markAreaDirty(m_rubberBand);
            m_rubberBand = QRect();
            return;
        }

        const QPointF pos = mapToViewport(event->pos());
        const QModelIndex index = indexAt(pos);

        if (index.isValid() && index == m_pressedIndex && !(event->modifiers() & Qt::ControlModifier)) {
            if (!m_doubleClick && KGlobalSettings::singleClick()) {
                emit activated(index);
                m_selectionModel->clearSelection();
                markAreaDirty(visibleArea());
            }
            // We don't clear and update the selection and current index in
            // mousePressEvent() if the item is already selected when it's pressed,
            // so we need to do that here.
            if (m_selectionModel->currentIndex() != index ||
                m_selectionModel->selectedIndexes().count() > 1) {
                m_selectionModel->select(index, QItemSelectionModel::ClearAndSelect);
                m_selectionModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
                markAreaDirty(visibleArea());
            }
            m_doubleClick = false;
            return;
        }
    }

    m_doubleClick = false;
    m_pressedIndex = QModelIndex();
}

void IconView::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // So we don't activate the item again on the release event
    m_doubleClick = true;

    // We don't want to invoke the default implementation in this case, since it
    // calls mousePressEvent().
    if (KGlobalSettings::singleClick()) {
        return;
    }

    const QModelIndex index = indexAt(mapToViewport(event->pos()));
    if (!index.isValid()) {
        return;
    }

    // Activate the item
    emit activated(index);

    m_selectionModel->clearSelection();
    markAreaDirty(visibleArea());
}

void IconView::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }

    // If an item is pressed
    if (m_pressedIndex.isValid())
    {
        const QPointF point = event->pos() - event->buttonDownPos(Qt::LeftButton);
        if (point.toPoint().manhattanLength() >= QApplication::startDragDistance())
        {
            startDrag(m_buttonDownPos, event->widget());
        }
        return;
    }

    const QPointF pos = mapToViewport(event->pos());
    const QRectF rubberBand = QRectF(m_buttonDownPos, pos).normalized();
    const QRect r = QRectF(rubberBand & m_viewportRect).toAlignedRect();

    if (r != m_rubberBand)
    {
        const QPoint pt = pos.toPoint();
        QRectF dirtyRect = m_rubberBand | r;
        m_rubberBand = r;

        dirtyRect |= visualRect(m_hoveredIndex);
        m_hoveredIndex = QModelIndex();

        foreach (const QModelIndex &index, m_selectionModel->selectedIndexes())
            dirtyRect |= visualRect(index);

        // Select the indexes inside the rubber band
        QItemSelection selection;
        for (int i = 0; i < m_items.size(); i++)
        {
            QModelIndex index = m_model->index(i, 0);
            if (!indexIntersectsRect(index, m_rubberBand))
                continue;

            int start = i;

            do {
                dirtyRect |= m_items[i].rect;
                if (m_items[i].rect.contains(pt) && visualRegion(index).contains(pt)) {
                    m_hoveredIndex = index;
                }
                index = m_model->index(++i, 0);
            } while (i < m_items.size() && indexIntersectsRect(index, m_rubberBand));

            selection.select(m_model->index(start, 0), m_model->index(i - 1, 0));
        }
        m_selectionModel->select(selection, QItemSelectionModel::ToggleCurrent);

        // Update the current index
        if (m_hoveredIndex.isValid()) {
            if (m_hoveredIndex != m_selectionModel->currentIndex()) {
                dirtyRect |= visualRect(m_selectionModel->currentIndex());
            }
            m_selectionModel->setCurrentIndex(m_hoveredIndex, QItemSelectionModel::NoUpdate);
        }
        markAreaDirty(dirtyRect);
    }
}

void IconView::wheelEvent(QGraphicsSceneWheelEvent *event)
{
    if ((event->modifiers() & Qt::CTRL) || (event->orientation() == Qt::Horizontal)) {
        // Let the event propagate to the parent widget
        event->ignore();
        return;
    }

    int pixels = 40 * event->delta() / 120;
    m_scrollBar->setValue(m_scrollBar->value() - pixels);
}

void IconView::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    const QPointF pos = mapToViewport(event->pos());
    const QModelIndex index = indexAt(pos);

    if (index.isValid()) {
        emit contextMenuRequest(event->widget(), event->screenPos());
    } else {
        // Let the event propagate to the parent widget
        event->ignore();
    }
}

void IconView::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
    event->setAccepted(KUrl::List::canDecode(event->mimeData()));
}

void IconView::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
    const QModelIndex index = indexAt(mapToViewport(event->pos()));
    if (index == m_hoveredIndex) {
        return;
    }

    const QString appletMimeType = static_cast<Plasma::Corona*>(scene())->appletMimeType();
    QRectF dirtyRect = visualRect(m_hoveredIndex);
    m_hoveredIndex = QModelIndex();

    if (index.isValid() && (m_model->flags(index) & Qt::ItemIsDropEnabled) &&
        !event->mimeData()->hasFormat(appletMimeType))
    {
        dirtyRect |= visualRect(index);
        bool onOurself = false;

        foreach (const QModelIndex &selected, m_selectionModel->selectedIndexes()) {
            if (selected == index) {
                onOurself = true;
                break;
            }
        }

        if (!onOurself) {
            m_hoveredIndex = index;
            dirtyRect |= visualRect(index);
        }
    }

    markAreaDirty(dirtyRect);
    event->setAccepted(!event->mimeData()->hasFormat(appletMimeType));
}

void IconView::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    // If the dropped item is an applet, let the parent widget handle it
    const QString appletMimeType = static_cast<Plasma::Corona*>(scene())->appletMimeType();
    if (event->mimeData()->hasFormat(appletMimeType)) {
        event->ignore();
        return;
    }

    event->accept();

    // Check if the drop event originated from this applet.
    // Normally we'd do this by checking if the source widget matches the target widget
    // in the drag and drop operation, but since two QGraphicsItems can be part of the
    // same widget, we can't use that method here.
    KFileItem item;
    if ((!m_dragInProgress && !m_hoveredIndex.isValid()) ||
        ((!m_dragInProgress || m_hoveredIndex.isValid()) &&
         m_model->flags(m_hoveredIndex) & Qt::ItemIsDropEnabled))
    {
        item = m_model->itemForIndex(m_hoveredIndex);
    }

    if (!item.isNull()) {
        QDropEvent ev(event->screenPos(), event->dropAction(), event->mimeData(),
                      event->buttons(), event->modifiers());
        //kDebug() << "dropping to" << m_url << "with" << view() << event->modifiers();
        KonqOperations::doDrop(item, m_dirModel->dirLister()->url(), &ev, event->widget());
        //kDebug() << "all done!";
        return;
    }

    // If we get to this point, the drag was started from within the applet,
    // so instead of moving/copying/linking the dropped URL's to the folder,
    // we'll move the items in the view.
    QPoint delta = (mapToViewport(event->pos()) - m_buttonDownPos).toPoint();
    if (delta.isNull() || m_iconsLocked) {
        return;
    }

    // If this option is set, we'll assume the dragged icons were aligned
    // to the grid before the drag started, and just adjust the delta we use
    // to move all of them.
    if (m_alignToGrid) {
        const QSize size = gridSize() + QSize(10, 10);
        if ((qAbs(delta.x()) < size.width() / 2) && (qAbs(delta.y()) < size.height() / 2)) {
            return;
        }

        delta.rx() = qRound(delta.x() / qreal(size.width()))  * size.width();
        delta.ry() = qRound(delta.y() / qreal(size.height())) * size.height();
    }

    QModelIndexList indexes;
    foreach (const QUrl &url, event->mimeData()->urls()) {
        const QModelIndex index = m_model->indexForUrl(url);
        if (index.isValid()) {
            m_items[index.row()].rect.translate(delta);
            indexes.append(index);
        }
    }

    // Make sure no icons have negative coordinates etc.
    doLayoutSanityCheck();
    updateScrollBar();
    markAreaDirty(visibleArea());
    m_regionCache.clear();

    m_layoutBroken = true;
    emit indexesMoved(indexes);
}

void IconView::changeEvent(QEvent *event)
{
    QGraphicsWidget::changeEvent(event);

    switch (event->type())
    {
    case QEvent::FontChange:
    case QEvent::ContentsRectChange:
        m_validRows = 0;
        m_delayedLayoutTimer.start(10, this);
        break;

    case QEvent::PaletteChange:
    case QEvent::StyleChange:
        markAreaDirty(visibleArea());
        update();
        break;

    default:
        break;
    }
}

// pos is the position where the mouse was clicked in the applet.
// widget is the widget that sent the mouse event that triggered the drag.
void IconView::startDrag(const QPointF &pos, QWidget *widget)
{
    QModelIndexList indexes = m_selectionModel->selectedIndexes();
    QRect boundingRect;
    foreach (const QModelIndex &index, indexes) {
        boundingRect |= visualRect(index);
    }

    QPixmap pixmap(boundingRect.size());
    pixmap.fill(Qt::transparent);

    QStyleOptionViewItemV4 option = viewOptions();
    option.state |= QStyle::State_Selected;

    updateTextShadows(palette().color(QPalette::HighlightedText));

    QPainter p(&pixmap);
    foreach (const QModelIndex &index, indexes)
    {
        option.rect = visualRect(index).translated(-boundingRect.topLeft());
        if (index == m_hoveredIndex)
            option.state |= QStyle::State_MouseOver;
        else
            option.state &= ~QStyle::State_MouseOver;
        m_delegate->paint(&p, option, index);
    }
    p.end();

    // Mark the area containing the about-to-be-dragged items as dirty, so they
    // will be erased from the view on the next repaint.  We have to do this
    // before calling QDrag::exec(), since it's a blocking call.
    markAreaDirty(boundingRect);

    // Unset the hovered index so dropEvent won't think the icons are being
    // dropped on a dragged folder.
    m_hoveredIndex = QModelIndex();
    m_dragInProgress = true;

    QDrag *drag = new QDrag(widget);
    drag->setMimeData(m_model->mimeData(indexes));
    drag->setPixmap(pixmap);
    drag->setHotSpot((pos - boundingRect.topLeft()).toPoint());
    drag->exec(m_model->supportedDragActions());

    m_dragInProgress = false;

    // Repaint the dragged icons in case the drag did not remove the file
    markAreaDirty(boundingRect);
}

QStyleOptionViewItemV4 IconView::viewOptions() const
{
    QStyleOptionViewItemV4 option;
    initStyleOption(&option);

    option.font                = font();
    option.decorationAlignment = Qt::AlignTop | Qt::AlignHCenter;
    option.decorationPosition  = QStyleOptionViewItem::Top;
    option.decorationSize      = iconSize();
    option.displayAlignment    = Qt::AlignHCenter;
    option.textElideMode       = Qt::ElideRight;
    option.locale              = QLocale::system();
    option.widget              = 0;
    option.viewItemPosition    = QStyleOptionViewItemV4::OnlyOne;

    if (m_wordWrap) {
        option.features = QStyleOptionViewItemV2::WrapText;
    }

    return option;
}

void IconView::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_delayedCacheClearTimer.timerId()) {
        m_delayedCacheClearTimer.stop();
        m_savedPositions.clear();
    } else if (event->timerId() == m_delayedLayoutTimer.timerId()) {
        m_delayedLayoutTimer.stop();
        layoutItems();
    }
}

#include "iconview.moc"

